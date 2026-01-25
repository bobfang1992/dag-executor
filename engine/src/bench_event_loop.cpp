#include "bench_event_loop.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "bench_stats.h"
#include "coro_task.h"
#include "event_loop.h"
#include "thread_pool.h"
#include "uv_sleep.h"

using json = nlohmann::ordered_json;
using namespace std::chrono;

namespace ranking {

// =============================================================================
// Mode A: posts - Post() throughput
// =============================================================================

struct PostsBenchResult {
  int total_posts = 0;
  int producers = 0;
  double wall_ms = 0.0;
  double posts_per_sec = 0.0;
  int64_t rss_start_kb = 0;
  int64_t rss_end_kb = 0;
};

static PostsBenchResult bench_posts(int n, int producers) {
  PostsBenchResult result;
  result.total_posts = n;
  result.producers = producers;
  result.rss_start_kb = get_current_rss_kb();

  EventLoop loop;
  loop.Start();

  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<bool> signaled{false};

  auto callback = [&]() {
    int prev = counter.fetch_add(1);
    if (prev == n - 1) {
      bool expected = false;
      if (signaled.compare_exchange_strong(expected, true)) {
        done.set_value();
      }
    }
  };

  auto start = steady_clock::now();

  if (producers == 1) {
    // Single producer: tight loop
    for (int i = 0; i < n; ++i) {
      loop.Post(callback);
    }
  } else {
    // Multi-producer: N threads, each posting n/producers callbacks
    std::vector<std::thread> threads;
    threads.reserve(producers);
    int per_thread = n / producers;
    int remainder = n % producers;

    for (int t = 0; t < producers; ++t) {
      int count = per_thread + (t < remainder ? 1 : 0);
      threads.emplace_back([&loop, &callback, count]() {
        for (int i = 0; i < count; ++i) {
          loop.Post(callback);
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }
  }

  done_future.wait();
  auto end = steady_clock::now();

  loop.Stop();

  result.wall_ms = duration<double, std::milli>(end - start).count();
  result.posts_per_sec = static_cast<double>(n) / (result.wall_ms / 1000.0);
  result.rss_end_kb = get_current_rss_kb();

  return result;
}

// =============================================================================
// Mode B: timers - Raw uv_timer_t throughput (actual timer scheduling)
// =============================================================================

struct TimersBenchResult {
  int total_timers = 0;
  int timeout_ms = 0;
  double wall_ms = 0.0;
  double timers_per_sec = 0.0;
  LatencyStats latency;
  int64_t rss_start_kb = 0;
  int64_t rss_end_kb = 0;
};

// State for raw timer benchmark - uses actual uv_timer_t scheduling
struct TimerBenchState {
  std::vector<double>* latencies;
  std::mutex* latencies_mutex;
  std::atomic<int>* completed;
  std::promise<void>* done;
  int total;
  steady_clock::time_point start_time;
};

static void OnTimerBenchCallback(uv_timer_t* t) {
  auto end = steady_clock::now();
  auto* state = static_cast<TimerBenchState*>(t->data);

  double latency_us = duration<double, std::micro>(end - state->start_time).count();
  {
    std::lock_guard<std::mutex> lock(*state->latencies_mutex);
    state->latencies->push_back(latency_us);
  }

  int prev = state->completed->fetch_add(1);
  if (prev == state->total - 1) {
    state->done->set_value();
  }

  // Clean up timer
  uv_timer_stop(t);
  uv_close(reinterpret_cast<uv_handle_t*>(t), [](uv_handle_t* h) {
    auto* timer = reinterpret_cast<uv_timer_t*>(h);
    delete static_cast<TimerBenchState*>(timer->data);
    delete timer;
  });
}

static TimersBenchResult bench_timers(int n, int timeout_ms) {
  TimersBenchResult result;
  result.total_timers = n;
  result.timeout_ms = timeout_ms;
  result.rss_start_kb = get_current_rss_kb();

  EventLoop loop;
  loop.Start();

  std::vector<double> latencies;
  latencies.reserve(n);
  std::mutex latencies_mutex;
  std::atomic<int> completed{0};
  std::promise<void> done;
  auto done_future = done.get_future();

  auto start = steady_clock::now();

  // Schedule all timers via Post to loop thread
  for (int i = 0; i < n; ++i) {
    bool posted = loop.Post([&loop, &latencies, &latencies_mutex, &completed, &done, n, timeout_ms]() {
      auto* timer = new uv_timer_t;
      auto* state = new TimerBenchState{
          &latencies, &latencies_mutex, &completed, &done, n, steady_clock::now()};
      timer->data = state;
      uv_timer_init(loop.RawLoop(), timer);
      uv_timer_start(timer, OnTimerBenchCallback, static_cast<uint64_t>(timeout_ms), 0);
    });
    if (!posted) {
      std::cerr << "Error: loop.Post() failed in timers benchmark" << std::endl;
      loop.Stop();
      return result;
    }
  }

  done_future.wait();
  auto end = steady_clock::now();

  loop.Stop();

  result.wall_ms = duration<double, std::milli>(end - start).count();
  result.timers_per_sec = static_cast<double>(n) / (result.wall_ms / 1000.0);
  result.latency = compute_latency_stats(latencies);
  result.rss_end_kb = get_current_rss_kb();

  return result;
}

// =============================================================================
// Mode C: sleep_vs_pool - IO-bound comparison
// =============================================================================

struct SleepVsPoolResult {
  int tasks = 0;
  int sleep_ms = 0;

  // Coroutine path
  double coro_wall_ms = 0.0;
  LatencyStats coro_latency;

  // ThreadPool path
  double pool_wall_ms = 0.0;
  LatencyStats pool_latency;

  double speedup_ratio = 0.0;
  int64_t rss_start_kb = 0;
  int64_t rss_end_kb = 0;
};

// Helper coroutine for sleep benchmark
static Task<void> sleep_coro(EventLoop& loop, int sleep_ms, std::vector<double>& latencies,
                             std::mutex& latencies_mutex, std::atomic<int>& completed,
                             std::promise<void>& done, int total) {
  auto start = steady_clock::now();
  co_await SleepMs(loop, sleep_ms);
  auto end = steady_clock::now();

  double latency_us = duration<double, std::micro>(end - start).count();
  {
    std::lock_guard<std::mutex> lock(latencies_mutex);
    latencies.push_back(latency_us);
  }

  int prev = completed.fetch_add(1);
  if (prev == total - 1) {
    done.set_value();
  }
}

static SleepVsPoolResult bench_sleep_vs_pool(int tasks_count, int sleep_ms) {
  SleepVsPoolResult result;
  result.tasks = tasks_count;
  result.sleep_ms = sleep_ms;
  result.rss_start_kb = get_current_rss_kb();

  // Part C1: EventLoop coroutines
  {
    EventLoop loop;
    loop.Start();

    std::vector<double> latencies;
    latencies.reserve(tasks_count);
    std::mutex latencies_mutex;
    std::atomic<int> completed{0};
    std::promise<void> done;
    auto done_future = done.get_future();

    std::vector<Task<void>> coros;
    coros.reserve(tasks_count);
    for (int i = 0; i < tasks_count; ++i) {
      coros.push_back(
          sleep_coro(loop, sleep_ms, latencies, latencies_mutex, completed, done, tasks_count));
    }

    auto start = steady_clock::now();

    for (auto& coro : coros) {
      if (!loop.Post([&coro]() { coro.start(); })) {
        std::cerr << "Error: loop.Post() failed in sleep_vs_pool (coro path)" << std::endl;
        return result;  // Return partial result on failure
      }
    }

    done_future.wait();
    auto end = steady_clock::now();

    loop.Stop();

    result.coro_wall_ms = duration<double, std::milli>(end - start).count();
    result.coro_latency = compute_latency_stats(latencies);
  }

  // Part C2: ThreadPool sleep_for
  {
    // Create a pool with number of threads equal to hardware concurrency
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4;
    }
    rankd::ThreadPool pool(num_threads);

    std::vector<double> latencies;
    latencies.reserve(tasks_count);
    std::mutex latencies_mutex;
    std::atomic<int> completed{0};
    std::promise<void> done;
    auto done_future = done.get_future();

    auto start = steady_clock::now();

    for (int i = 0; i < tasks_count; ++i) {
      pool.submit([&, sleep_ms, tasks_count]() {
        auto task_start = steady_clock::now();
        std::this_thread::sleep_for(milliseconds(sleep_ms));
        auto task_end = steady_clock::now();

        double latency_us = duration<double, std::micro>(task_end - task_start).count();
        {
          std::lock_guard<std::mutex> lock(latencies_mutex);
          latencies.push_back(latency_us);
        }

        int prev = completed.fetch_add(1);
        if (prev == tasks_count - 1) {
          done.set_value();
        }
      });
    }

    done_future.wait();
    auto end = steady_clock::now();

    pool.wait_idle();

    result.pool_wall_ms = duration<double, std::milli>(end - start).count();
    result.pool_latency = compute_latency_stats(latencies);
  }

  result.speedup_ratio = result.pool_wall_ms / result.coro_wall_ms;
  result.rss_end_kb = get_current_rss_kb();

  return result;
}

// =============================================================================
// Output formatting
// =============================================================================

static void format_throughput(std::ostream& os, double value) {
  if (value >= 1e6) {
    os << std::fixed << std::setprecision(2) << (value / 1e6) << "M";
  } else if (value >= 1e3) {
    os << std::fixed << std::setprecision(2) << (value / 1e3) << "K";
  } else {
    os << std::fixed << std::setprecision(1) << value;
  }
}

static void print_posts_human(const PostsBenchResult& r) {
  std::cout << "=== EventLoop Benchmark: posts ===" << std::endl;
  std::cout << "  Total posts:     " << r.total_posts << std::endl;
  std::cout << "  Producers:       " << r.producers << std::endl;
  std::cout << "  Wall time:       " << std::fixed << std::setprecision(1) << r.wall_ms << " ms"
            << std::endl;
  std::cout << "  Throughput:      ";
  format_throughput(std::cout, r.posts_per_sec);
  std::cout << " posts/sec" << std::endl;
  std::cout << "  RSS start/end:   " << std::fixed << std::setprecision(1)
            << (r.rss_start_kb / 1024.0) << " / " << (r.rss_end_kb / 1024.0) << " MB" << std::endl;
  std::cout << std::endl;
}

static void print_timers_human(const TimersBenchResult& r) {
  std::cout << "=== EventLoop Benchmark: timers (uv_timer " << r.timeout_ms << "ms) ===" << std::endl;
  std::cout << "  Total timers:    " << r.total_timers << std::endl;
  std::cout << "  Timeout:         " << r.timeout_ms << " ms" << std::endl;
  std::cout << "  Wall time:       " << std::fixed << std::setprecision(1) << r.wall_ms << " ms"
            << std::endl;
  std::cout << "  Throughput:      ";
  format_throughput(std::cout, r.timers_per_sec);
  std::cout << " timers/sec" << std::endl;
  std::cout << "  Latency p50/p90/p99: " << std::fixed << std::setprecision(0) << r.latency.p50_us
            << "/" << r.latency.p90_us << "/" << r.latency.p99_us << " us" << std::endl;
  std::cout << "  Latency max/mean:    " << std::fixed << std::setprecision(0) << r.latency.max_us
            << "/" << r.latency.mean_us << " us" << std::endl;
  std::cout << "  RSS start/end:   " << std::fixed << std::setprecision(1)
            << (r.rss_start_kb / 1024.0) << " / " << (r.rss_end_kb / 1024.0) << " MB" << std::endl;
  std::cout << std::endl;
}

static void print_sleep_vs_pool_human(const SleepVsPoolResult& r) {
  std::cout << "=== EventLoop Benchmark: sleep_vs_pool ===" << std::endl;
  std::cout << "  Tasks:           " << r.tasks << std::endl;
  std::cout << "  Sleep:           " << r.sleep_ms << " ms" << std::endl;
  std::cout << std::endl;

  std::cout << "  Coroutine path:" << std::endl;
  std::cout << "    Wall time:     " << std::fixed << std::setprecision(1) << r.coro_wall_ms
            << " ms" << std::endl;
  std::cout << "    Latency p50/p90/p99: " << std::fixed << std::setprecision(0)
            << r.coro_latency.p50_us << "/" << r.coro_latency.p90_us << "/"
            << r.coro_latency.p99_us << " us" << std::endl;
  std::cout << std::endl;

  std::cout << "  ThreadPool path:" << std::endl;
  std::cout << "    Wall time:     " << std::fixed << std::setprecision(1) << r.pool_wall_ms
            << " ms" << std::endl;
  std::cout << "    Latency p50/p90/p99: " << std::fixed << std::setprecision(0)
            << r.pool_latency.p50_us << "/" << r.pool_latency.p90_us << "/"
            << r.pool_latency.p99_us << " us" << std::endl;
  std::cout << std::endl;

  std::cout << "  Speedup (pool/coro): " << std::fixed << std::setprecision(1) << r.speedup_ratio
            << "x" << std::endl;
  std::cout << "  RSS start/end:   " << std::fixed << std::setprecision(1)
            << (r.rss_start_kb / 1024.0) << " / " << (r.rss_end_kb / 1024.0) << " MB" << std::endl;
  std::cout << std::endl;
}

static json latency_to_json(const LatencyStats& l) {
  json j;
  j["min_us"] = l.min_us;
  j["max_us"] = l.max_us;
  j["mean_us"] = l.mean_us;
  j["p50_us"] = l.p50_us;
  j["p90_us"] = l.p90_us;
  j["p99_us"] = l.p99_us;
  j["count"] = l.count;
  return j;
}

static json posts_to_json(const PostsBenchResult& r) {
  json j;
  j["total_posts"] = r.total_posts;
  j["producers"] = r.producers;
  j["wall_ms"] = r.wall_ms;
  j["posts_per_sec"] = r.posts_per_sec;
  j["rss_start_kb"] = r.rss_start_kb;
  j["rss_end_kb"] = r.rss_end_kb;
  return j;
}

static json timers_to_json(const TimersBenchResult& r) {
  json j;
  j["total_timers"] = r.total_timers;
  j["timeout_ms"] = r.timeout_ms;
  j["wall_ms"] = r.wall_ms;
  j["timers_per_sec"] = r.timers_per_sec;
  j["latency"] = latency_to_json(r.latency);
  j["rss_start_kb"] = r.rss_start_kb;
  j["rss_end_kb"] = r.rss_end_kb;
  return j;
}

static json sleep_vs_pool_to_json(const SleepVsPoolResult& r) {
  json j;
  j["tasks"] = r.tasks;
  j["sleep_ms"] = r.sleep_ms;
  j["coro_wall_ms"] = r.coro_wall_ms;
  j["coro_latency"] = latency_to_json(r.coro_latency);
  j["pool_wall_ms"] = r.pool_wall_ms;
  j["pool_latency"] = latency_to_json(r.pool_latency);
  j["speedup_ratio"] = r.speedup_ratio;
  j["rss_start_kb"] = r.rss_start_kb;
  j["rss_end_kb"] = r.rss_end_kb;
  return j;
}

// =============================================================================
// Main entry point
// =============================================================================

int run_bench_eventloop(const BenchEventLoopConfig& config) {
  bool run_posts = (config.mode == "all" || config.mode == "posts");
  bool run_timers = (config.mode == "all" || config.mode == "timers");
  bool run_sleep = (config.mode == "all" || config.mode == "sleep_vs_pool");

  // Validate mode
  if (!run_posts && !run_timers && !run_sleep) {
    std::cerr << "Error: invalid bench_eventloop_mode '" << config.mode << "'" << std::endl;
    std::cerr << "Valid modes: posts, timers, sleep_vs_pool, all" << std::endl;
    return 1;
  }

  json json_output;

  // Mode A: posts
  if (run_posts) {
    int n = config.n;
    if (n == 0) {
      n = 1000000;  // 1M posts - same for all producer counts for fair comparison
    }

    auto result = bench_posts(n, config.producers);

    if (config.json_output) {
      json_output["posts"] = posts_to_json(result);
    } else {
      print_posts_human(result);
    }
  }

  // Mode B: timers (raw uv_timer_t scheduling)
  if (run_timers) {
    int n = config.n;
    if (n == 0) {
      n = 10000;
    }

    // Use config.sleep_ms for timer timeout (default 0 = immediate fire)
    auto result = bench_timers(n, config.sleep_ms);

    if (config.json_output) {
      json_output["timers"] = timers_to_json(result);
    } else {
      print_timers_human(result);
    }
  }

  // Mode C: sleep_vs_pool
  if (run_sleep) {
    auto result = bench_sleep_vs_pool(config.tasks, config.sleep_ms);

    if (config.json_output) {
      json_output["sleep_vs_pool"] = sleep_vs_pool_to_json(result);
    } else {
      print_sleep_vs_pool_human(result);
    }
  }

  if (config.json_output) {
    std::cout << json_output.dump(2) << std::endl;
  }

  return 0;
}

}  // namespace ranking
