// Folly micro-benchmarks v2
// Methodology: 2 warm-up runs + 3 measured runs, report median
// Tests: post throughput, timer throughput (1ms), fan-out coroutine sleep (1ms)
//
// Build (with vcpkg):
//   g++-13 -std=c++20 -O3 -o bench_folly bench_folly.cpp \
//     -I$HOME/vcpkg/installed/x64-linux/include \
//     -L$HOME/vcpkg/installed/x64-linux/lib \
//     -lfolly -lfmt -lglog -lgflags -ldouble-conversion -levent -lpthread -ldl \
//     -Wl,-rpath,$HOME/vcpkg/installed/x64-linux/lib
//
// Run: taskset -c 0-1 ./bench_folly

#include <folly/init/Init.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>
#include <folly/futures/Future.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/BlockingWait.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <sys/resource.h>

using clock_t_ = std::chrono::steady_clock;

static int64_t get_rss_kb() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

struct LatStats {
    double min_us, max_us, mean_us, p50_us, p90_us, p99_us;
    size_t count;
};

static LatStats compute_stats(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    return { v.front(), v.back(), sum / v.size(),
             v[v.size()*50/100], v[v.size()*90/100], v[v.size()*99/100], v.size() };
}

static void print_lat(const char* prefix, const LatStats& s) {
    std::cout << "    \"" << prefix << "latency\": {\n"
              << "      \"min_us\": " << s.min_us << ",\n"
              << "      \"max_us\": " << s.max_us << ",\n"
              << "      \"mean_us\": " << s.mean_us << ",\n"
              << "      \"p50_us\": " << s.p50_us << ",\n"
              << "      \"p90_us\": " << s.p90_us << ",\n"
              << "      \"p99_us\": " << s.p99_us << ",\n"
              << "      \"count\": " << s.count << "\n"
              << "    }";
}

// ============================================================
// 1. Post throughput: cross-thread runInEventBaseThread
// ============================================================
struct PostResult { double wall_ms; double ops_per_sec; };

PostResult run_post(int total_posts) {
    folly::EventBase evb;
    std::atomic<int> count{0};

    // Run event base on a separate thread
    std::thread runner([&] { evb.loopForever(); });

    auto start = clock_t_::now();
    for (int i = 0; i < total_posts; ++i) {
        evb.runInEventBaseThread([&] {
            count.fetch_add(1, std::memory_order_relaxed);
        });
    }
    while (count.load(std::memory_order_relaxed) < total_posts) {
        std::this_thread::yield();
    }
    auto elapsed = std::chrono::duration<double, std::milli>(clock_t_::now() - start).count();

    evb.terminateLoopSoon();
    runner.join();
    return { elapsed, total_posts / (elapsed / 1000.0) };
}

void bench_post(int total_posts, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) run_post(total_posts);
    std::vector<PostResult> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_post(total_posts));
    std::sort(results.begin(), results.end(), [](auto&a,auto&b){return a.wall_ms<b.wall_ms;});
    auto& med = results[runs/2];
    std::cout << "  \"posts\": {\n"
              << "    \"total_posts\": " << total_posts << ",\n"
              << "    \"warmup_runs\": " << warmup << ",\n"
              << "    \"measured_runs\": " << runs << ",\n"
              << "    \"wall_ms\": " << med.wall_ms << ",\n"
              << "    \"posts_per_sec\": " << med.ops_per_sec << ",\n"
              << "    \"rss_kb\": " << get_rss_kb() << "\n"
              << "  }";
}

// ============================================================
// 2. Timer throughput: HHWheelTimer with 1ms timeout
// ============================================================
struct TimerResult { double wall_ms; double timers_per_sec; LatStats lat; };

class BenchTimerCallback : public folly::HHWheelTimer::Callback {
public:
    BenchTimerCallback(std::vector<double>* lats, std::mutex* mu,
                       std::atomic<int>* fired, int total,
                       folly::EventBase* evb, clock_t_::time_point created)
        : lats_(lats), mu_(mu), fired_(fired), total_(total),
          evb_(evb), created_(created) {}

    void timeoutExpired() noexcept override {
        auto lat = std::chrono::duration<double, std::micro>(
            clock_t_::now() - created_).count();
        { std::lock_guard<std::mutex> lk(*mu_); lats_->push_back(lat); }
        if (fired_->fetch_add(1, std::memory_order_relaxed) == total_ - 1) {
            evb_->terminateLoopSoon();
        }
        delete this;
    }

    void callbackCanceled() noexcept override { delete this; }

private:
    std::vector<double>* lats_;
    std::mutex* mu_;
    std::atomic<int>* fired_;
    int total_;
    folly::EventBase* evb_;
    clock_t_::time_point created_;
};

TimerResult run_timers(int total_timers) {
    folly::EventBase evb;
    std::vector<double> latencies;
    latencies.reserve(total_timers);
    std::mutex mu;
    std::atomic<int> fired{0};

    auto start = clock_t_::now();

    // Schedule all timers from the event base thread
    evb.runInEventBaseThread([&] {
        for (int i = 0; i < total_timers; ++i) {
            auto* cb = new BenchTimerCallback(
                &latencies, &mu, &fired, total_timers, &evb, clock_t_::now());
            evb.timer().scheduleTimeout(cb, std::chrono::milliseconds(1));
        }
    });

    evb.loopForever();

    auto elapsed = std::chrono::duration<double, std::milli>(clock_t_::now() - start).count();
    auto stats = compute_stats(latencies);
    return { elapsed, total_timers / (elapsed / 1000.0), stats };
}

void bench_timers(int total_timers, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) run_timers(total_timers);
    std::vector<TimerResult> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_timers(total_timers));
    std::sort(results.begin(), results.end(), [](auto&a,auto&b){return a.wall_ms<b.wall_ms;});
    auto& med = results[runs/2];
    std::cout << "  \"timers\": {\n"
              << "    \"total_timers\": " << total_timers << ",\n"
              << "    \"warmup_runs\": " << warmup << ",\n"
              << "    \"measured_runs\": " << runs << ",\n"
              << "    \"wall_ms\": " << med.wall_ms << ",\n"
              << "    \"timers_per_sec\": " << med.timers_per_sec << ",\n";
    print_lat("", med.lat);
    std::cout << ",\n"
              << "    \"rss_kb\": " << get_rss_kb() << "\n"
              << "  }";
}

// ============================================================
// 3. Fan-out coroutine sleep (1ms) via collectAll + thread pool
// ============================================================
struct SleepResult {
    double coro_wall_ms; LatStats coro_lat;
    double pool_wall_ms; LatStats pool_lat;
    double speedup;
};

SleepResult run_sleep(int num_tasks, int sleep_ms) {
    // Part 1: coroutine path using folly::coro
    std::vector<double> latencies;
    latencies.reserve(num_tasks);
    std::mutex mu;

    auto make_sleep_task = [&](int ms) -> folly::coro::Task<void> {
        auto start = clock_t_::now();
        co_await folly::coro::sleep(std::chrono::milliseconds(ms));
        auto lat = std::chrono::duration<double, std::micro>(
            clock_t_::now() - start).count();
        { std::lock_guard<std::mutex> lk(mu); latencies.push_back(lat); }
    };

    auto coro_start = clock_t_::now();

    // collectAll runs all tasks concurrently on the executor
    auto all_task = [&]() -> folly::coro::Task<void> {
        std::vector<folly::coro::Task<void>> tasks;
        tasks.reserve(num_tasks);
        for (int i = 0; i < num_tasks; ++i) {
            tasks.push_back(make_sleep_task(sleep_ms));
        }
        co_await folly::coro::collectAllRange(std::move(tasks));
    };

    folly::coro::blockingWait(all_task());

    auto coro_wall = std::chrono::duration<double, std::milli>(
        clock_t_::now() - coro_start).count();
    auto coro_lat = compute_stats(latencies);

    // Part 2: thread pool path
    std::vector<double> pool_latencies;
    pool_latencies.reserve(num_tasks);

    folly::CPUThreadPoolExecutor pool(8);
    auto pool_start = clock_t_::now();

    std::vector<folly::Future<folly::Unit>> futures;
    futures.reserve(num_tasks);
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(folly::via(&pool, [&] {
            auto start = clock_t_::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            auto lat = std::chrono::duration<double, std::micro>(
                clock_t_::now() - start).count();
            { std::lock_guard<std::mutex> lk(mu); pool_latencies.push_back(lat); }
        }));
    }
    folly::collectAll(std::move(futures)).get();

    auto pool_wall = std::chrono::duration<double, std::milli>(
        clock_t_::now() - pool_start).count();
    auto pool_lat = compute_stats(pool_latencies);

    return { coro_wall, coro_lat, pool_wall, pool_lat, pool_wall / coro_wall };
}

void bench_sleep(int num_tasks, int sleep_ms, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) run_sleep(num_tasks, sleep_ms);
    std::vector<SleepResult> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_sleep(num_tasks, sleep_ms));
    std::sort(results.begin(), results.end(), [](auto&a,auto&b){return a.coro_wall_ms<b.coro_wall_ms;});
    auto& med = results[runs/2];
    std::cout << "  \"sleep_vs_pool\": {\n"
              << "    \"tasks\": " << num_tasks << ",\n"
              << "    \"sleep_ms\": " << sleep_ms << ",\n"
              << "    \"warmup_runs\": " << warmup << ",\n"
              << "    \"measured_runs\": " << runs << ",\n"
              << "    \"coro_wall_ms\": " << med.coro_wall_ms << ",\n";
    print_lat("coro_", med.coro_lat);
    std::cout << ",\n"
              << "    \"pool_wall_ms\": " << med.pool_wall_ms << ",\n";
    print_lat("pool_", med.pool_lat);
    std::cout << ",\n"
              << "    \"speedup_ratio\": " << med.speedup << ",\n"
              << "    \"rss_kb\": " << get_rss_kb() << "\n"
              << "  }";
}

int main(int argc, char** argv) {
    folly::init(&argc, &argv);

    const int WARMUP = 2, RUNS = 3;
    std::cout << "{\n";
    std::cout << "  \"framework\": \"folly\",\n";
    std::cout << "  \"system\": { \"cpus\": " << std::thread::hardware_concurrency() << " },\n";
    bench_post(1000000, WARMUP, RUNS);
    std::cout << ",\n";
    bench_timers(10000, WARMUP, RUNS);
    std::cout << ",\n";
    bench_sleep(1000, 1, WARMUP, RUNS);
    std::cout << "\n}\n";
    return 0;
}
