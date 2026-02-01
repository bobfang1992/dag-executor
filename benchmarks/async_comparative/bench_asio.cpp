// Boost.Asio micro-benchmarks v2
// Methodology: 2 warm-up runs + 3 measured runs, report median
// Tests: post throughput, timer throughput (1ms), fan-out coroutine sleep (1ms)
//
// Build: g++-13 -std=c++20 -O3 -o bench_asio bench_asio_v2.cpp -lpthread
// Run:   taskset -c 0-1 ./bench_asio

#include <utility>  // std::exchange (needed for older Boost)
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <sys/resource.h>

namespace asio = boost::asio;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using steady_timer = asio::steady_timer;
using clock_t_ = std::chrono::steady_clock;

// ============================================================
// RSS helper
// ============================================================
static int64_t get_rss_kb() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    // Linux: ru_maxrss is in KB
    return ru.ru_maxrss;
}

// ============================================================
// Latency stats
// ============================================================
struct LatStats {
    double min_us, max_us, mean_us, p50_us, p90_us, p99_us;
    size_t count;
};

static LatStats compute_stats(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    return {
        v.front(), v.back(), sum / v.size(),
        v[v.size() * 50 / 100], v[v.size() * 90 / 100], v[v.size() * 99 / 100],
        v.size()
    };
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
// 1. Post throughput
// ============================================================
struct PostResult { double wall_ms; double ops_per_sec; };

PostResult run_post(int total_posts) {
    asio::io_context io;
    auto work = asio::make_work_guard(io);
    std::atomic<int> count{0};

    std::thread runner([&] { io.run(); });
    auto start = clock_t_::now();

    for (int i = 0; i < total_posts; ++i) {
        asio::post(io, [&] { count.fetch_add(1, std::memory_order_relaxed); });
    }
    while (count.load(std::memory_order_relaxed) < total_posts) {
        std::this_thread::yield();
    }

    auto elapsed = std::chrono::duration<double, std::milli>(clock_t_::now() - start).count();
    work.reset();
    runner.join();
    return { elapsed, total_posts / (elapsed / 1000.0) };
}

void bench_post(int total_posts, int warmup, int runs) {
    // Warm up
    for (int i = 0; i < warmup; ++i) run_post(total_posts);

    std::vector<PostResult> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_post(total_posts));

    // Sort by wall_ms, take median
    std::sort(results.begin(), results.end(), [](auto& a, auto& b){ return a.wall_ms < b.wall_ms; });
    auto& med = results[runs / 2];

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
// 2. Timer throughput (1ms timeout)
// ============================================================
struct TimerResult { double wall_ms; double timers_per_sec; LatStats lat; };

TimerResult run_timers(int total_timers) {
    asio::io_context io;
    std::atomic<int> fired{0};
    std::vector<double> latencies;
    latencies.reserve(total_timers);
    std::mutex mu;

    auto start = clock_t_::now();
    for (int i = 0; i < total_timers; ++i) {
        auto* timer = new steady_timer(io, std::chrono::milliseconds(1));
        auto created = clock_t_::now();
        timer->async_wait([&, timer, created](const boost::system::error_code&) {
            auto lat = std::chrono::duration<double, std::micro>(clock_t_::now() - created).count();
            { std::lock_guard<std::mutex> lk(mu); latencies.push_back(lat); }
            fired.fetch_add(1, std::memory_order_relaxed);
            delete timer;
        });
    }
    io.run();

    auto elapsed = std::chrono::duration<double, std::milli>(clock_t_::now() - start).count();
    auto stats = compute_stats(latencies);
    return { elapsed, total_timers / (elapsed / 1000.0), stats };
}

void bench_timers(int total_timers, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) run_timers(total_timers);

    std::vector<TimerResult> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_timers(total_timers));

    std::sort(results.begin(), results.end(), [](auto& a, auto& b){ return a.wall_ms < b.wall_ms; });
    auto& med = results[runs / 2];

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
// 3. Fan-out coroutine sleep (1ms) + thread pool comparison
// ============================================================
struct SleepResult {
    double coro_wall_ms; LatStats coro_lat;
    double pool_wall_ms; LatStats pool_lat;
    double speedup;
};

SleepResult run_sleep(int num_tasks, int sleep_ms) {
    // Part 1: coroutine path
    asio::io_context io;
    std::atomic<int> done{0};
    std::vector<double> latencies;
    latencies.reserve(num_tasks);
    std::mutex mu;

    auto wall_start = clock_t_::now();
    for (int i = 0; i < num_tasks; ++i) {
        co_spawn(io, [&]() -> asio::awaitable<void> {
            auto start = clock_t_::now();
            steady_timer timer(co_await asio::this_coro::executor);
            timer.expires_after(std::chrono::milliseconds(sleep_ms));
            co_await timer.async_wait(use_awaitable);
            auto lat = std::chrono::duration<double, std::micro>(clock_t_::now() - start).count();
            { std::lock_guard<std::mutex> lk(mu); latencies.push_back(lat); }
            done.fetch_add(1, std::memory_order_relaxed);
        }, detached);
    }
    io.run();
    auto coro_wall = std::chrono::duration<double, std::milli>(clock_t_::now() - wall_start).count();
    auto coro_lat = compute_stats(latencies);

    // Part 2: thread pool path
    std::vector<double> pool_latencies;
    pool_latencies.reserve(num_tasks);

    auto pool_start = clock_t_::now();
    asio::thread_pool pool(8);
    for (int i = 0; i < num_tasks; ++i) {
        asio::post(pool, [&] {
            auto start = clock_t_::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            auto lat = std::chrono::duration<double, std::micro>(clock_t_::now() - start).count();
            { std::lock_guard<std::mutex> lk(mu); pool_latencies.push_back(lat); }
        });
    }
    pool.join();
    auto pool_wall = std::chrono::duration<double, std::milli>(clock_t_::now() - pool_start).count();
    auto pool_lat = compute_stats(pool_latencies);

    return { coro_wall, coro_lat, pool_wall, pool_lat, pool_wall / coro_wall };
}

void bench_sleep(int num_tasks, int sleep_ms, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) run_sleep(num_tasks, sleep_ms);

    std::vector<SleepResult> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_sleep(num_tasks, sleep_ms));

    std::sort(results.begin(), results.end(), [](auto& a, auto& b){ return a.coro_wall_ms < b.coro_wall_ms; });
    auto& med = results[runs / 2];

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

int main() {
    const int WARMUP = 2, RUNS = 3;
    std::cout << "{\n";
    std::cout << "  \"framework\": \"boost_asio\",\n";
    std::cout << "  \"system\": { \"cpus\": " << std::thread::hardware_concurrency() << " },\n";
    bench_post(1000000, WARMUP, RUNS);
    std::cout << ",\n";
    bench_timers(10000, WARMUP, RUNS);
    std::cout << ",\n";
    bench_sleep(1000, 1, WARMUP, RUNS);
    std::cout << "\n}\n";
    return 0;
}
