# EventLoop Benchmarking

This document describes the EventLoop micro-benchmarking capabilities added to `rankd`.

## Overview

The `--bench_eventloop` flag enables offline benchmarking of the EventLoop infrastructure without requiring Redis, plans, or any external dependencies. This is useful for:

- Measuring Post() throughput and queue efficiency
- Profiling timer callback scheduling
- Comparing coroutine-based IO vs traditional thread pool approaches
- Tracking latency distributions and memory usage

## CLI Flags

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--bench_eventloop` | bool | false | Enable EventLoop benchmark mode |
| `--bench_eventloop_mode` | string | "all" | Benchmark mode: `posts`, `timers`, `sleep_vs_pool`, or `all` |
| `--bench_n` | int | 0 (auto) | Number of operations (0 uses mode-specific defaults) |
| `--bench_producers` | int | 1 | Number of producer threads for `posts` mode |
| `--bench_sleep_ms` | int | 1 | Sleep/timer duration in ms (0 allowed for timers mode) |
| `--bench_tasks` | int | 1000 | Number of concurrent tasks for timer/sleep modes |
| `--bench_json` | bool | false | Output results as JSON |

## Benchmark Modes

### Mode A: `posts` - Post() Throughput

Measures the throughput of `EventLoop::Post()` with configurable producer thread counts.

**Purpose**: Benchmark queue + wakeup overhead with 1 or N producer threads.

**Pattern**:
- Single producer: tight loop of `Post()` calls from one thread
- Multi-producer: N threads, each posting `n/N` callbacks

**Default n**: 1,000,000 (same for all producer counts for fair comparison)

**Output**: total_posts, producers, wall_ms, posts_per_sec, rss_start/end

```bash
# Single producer (default)
engine/bin/rankd --bench_eventloop --bench_eventloop_mode posts

# 4 producer threads
engine/bin/rankd --bench_eventloop --bench_eventloop_mode posts --bench_producers 4

# Custom iteration count
engine/bin/rankd --bench_eventloop --bench_eventloop_mode posts --bench_n 200000
```

### Mode B: `timers` - Timer Callback Throughput

Measures timer scheduling and callback execution via raw `uv_timer_t`.

**Purpose**: Benchmark timer creation, scheduling, and callback dispatch through the full libuv timer path.

**Pattern**: Creates N raw `uv_timer_t` handles with the specified timeout, measuring latency from timer start to callback execution. Uses `--bench_sleep_ms` to set the timer delay (0 = immediate fire but still goes through full timer scheduling).

**Note**: This mode uses raw libuv timers instead of `SleepMs()` because `SleepMs(0)` has an optimization where `await_ready()` returns true, bypassing actual timer scheduling. Raw timers ensure we measure real timer overhead.

**Default n**: 10,000 timers, **Default timeout**: 0ms

**Output**: total_timers, wall_ms, timers_per_sec, latency (p50/p90/p99/max/mean), rss

```bash
# Default settings (0ms timers)
engine/bin/rankd --bench_eventloop --bench_eventloop_mode timers

# Custom task count
engine/bin/rankd --bench_eventloop --bench_eventloop_mode timers --bench_n 50000

# Non-zero timer delay (measures actual sleep accuracy)
engine/bin/rankd --bench_eventloop --bench_eventloop_mode timers --bench_sleep_ms 5
```

### Mode C: `sleep_vs_pool` - IO-Bound Comparison

Compares N concurrent sleeps between EventLoop coroutines and ThreadPool.

**Purpose**: Demonstrate the advantage of coroutine-based IO over thread-pool blocking IO for IO-bound workloads.

**Part C1 - EventLoop coroutines**:
- N concurrent `SleepMs(X)` coroutines
- All complete in ~X ms (plus overhead) regardless of N
- Single thread handles all sleeps via libuv timers

**Part C2 - ThreadPool sleep_for**:
- N tasks submitted to thread pool, each calls `sleep_for(X)`
- Wall time ~= ceil(N / threads) * X
- Limited by thread count

**Defaults**: tasks=1000, sleep_ms=1

**Output**: Per-path wall_ms, latency stats, speedup_ratio (pool/coro)

```bash
# Default settings
engine/bin/rankd --bench_eventloop --bench_eventloop_mode sleep_vs_pool

# Larger task count with longer sleep
engine/bin/rankd --bench_eventloop --bench_eventloop_mode sleep_vs_pool \
  --bench_tasks 5000 --bench_sleep_ms 5
```

## Output Formats

### Human-Readable (default)

```
=== Posts: 1 producer ===
  Total posts:     1000000
  Producers:       1
  Wall time:       34.1 ms
  Throughput:      29.3M posts/sec
  RSS start/end:   12.4 / 14.1 MB

=== Timers: 0ms ===
  Total timers:    10000
  Wall time:       1.9 ms
  Throughput:      5.23M timers/sec
  Latency p50/p90/p99: 1006/1119/1144 us
  Latency max/mean:    1250/1024 us
  RSS start/end:   14.1 / 16.2 MB

=== EventLoop Benchmark: sleep_vs_pool ===
  Tasks:           1000
  Sleep:           1 ms

  Coroutine path:
    Wall time:     8.2 ms
    Latency p50/p90/p99: 1050/1080/1120 us

  ThreadPool path:
    Wall time:     125.4 ms
    Latency p50/p90/p99: 1850/2400/3200 us

  Speedup (pool/coro): 15.3x
  RSS start/end:   16.2 / 18.5 MB
```

### JSON (`--bench_json`)

```json
{
  "posts": {
    "total_posts": 1000000,
    "producers": 1,
    "wall_ms": 34.1,
    "posts_per_sec": 29325513.2,
    "rss_start_kb": 12697,
    "rss_end_kb": 14438
  },
  "timers": {
    "total_timers": 10000,
    "timer_ms": 0,
    "wall_ms": 1.9,
    "timers_per_sec": 5230125.4,
    "latency": {
      "min_us": 980.1,
      "max_us": 1250.0,
      "mean_us": 1024.3,
      "p50_us": 1006.0,
      "p90_us": 1119.0,
      "p99_us": 1144.0,
      "count": 10000
    },
    "rss_start_kb": 14438,
    "rss_end_kb": 16589
  },
  "sleep_vs_pool": {
    "tasks": 1000,
    "sleep_ms": 1,
    "coro_wall_ms": 8.2,
    "coro_latency": { ... },
    "pool_wall_ms": 125.4,
    "pool_latency": { ... },
    "speedup_ratio": 15.3,
    "rss_start_kb": 16589,
    "rss_end_kb": 18944
  }
}
```

## Usage Examples

```bash
# Build
cmake --build engine/build --parallel

# Run all benchmarks (human output)
engine/bin/rankd --bench_eventloop

# Run all benchmarks (JSON output)
engine/bin/rankd --bench_eventloop --bench_json

# Test specific modes
engine/bin/rankd --bench_eventloop --bench_eventloop_mode posts --bench_n 50000
engine/bin/rankd --bench_eventloop --bench_eventloop_mode posts --bench_producers 4
engine/bin/rankd --bench_eventloop --bench_eventloop_mode timers --bench_n 5000
engine/bin/rankd --bench_eventloop --bench_eventloop_mode sleep_vs_pool --bench_tasks 500 --bench_sleep_ms 5

# Verify no Redis/plans required (offline)
# All above commands work without Redis running or artifacts present
```

## Interpreting Results

### posts Mode

- **posts_per_sec**: Higher is better. Typical values: 1-5M/sec on modern hardware.
- Scaling with producers shows lock contention in the queue.
- RSS growth indicates per-callback memory overhead.

### timers Mode

- **timers_per_sec**: Higher is better. Measures libuv timer efficiency.
- **Latency p99**: With 0ms timers, expect ~1ms (libuv timer granularity). With non-zero timers, latency should be close to the requested sleep time.
- This mode uses raw `uv_timer_t` to measure real timer scheduling overhead. Note: `SleepMs(0)` has an optimization that bypasses timer scheduling (`await_ready` returns true), so we use raw timers here.

### sleep_vs_pool Mode

- **speedup_ratio**: Shows how much faster coroutines are for IO-bound work.
- Expected: `pool_wall_ms ≈ ceil(tasks / threads) * sleep_ms`
- Expected: `coro_wall_ms ≈ sleep_ms + small_overhead`
- Coroutines win big when `tasks >> threads` (common in IO-heavy workloads).

## Implementation Details

### Files

| File | Purpose |
|------|---------|
| `engine/include/bench_stats.h` | LatencyStats struct, compute_latency_stats(), RSS helpers |
| `engine/include/bench_event_loop.h` | BenchEventLoopConfig, run_bench_eventloop() |
| `engine/src/bench_event_loop.cpp` | All benchmark implementations |

### Safe Coroutine Pattern

The benchmarks use the standard safe coroutine pattern where `Task<void>` objects are stored in a vector and kept alive until completion:

```cpp
std::vector<Task<void>> tasks;
for (int i = 0; i < n; ++i) {
  tasks.push_back(make_coro(loop, ...));  // Factory function
}
for (auto& task : tasks) {
  loop.Post([&task]() { task.start(); });
}
// Wait for completion...
// Tasks go out of scope safely after completion
```

### RSS Measurement

- macOS: Uses `mach_task_basic_info` for accurate current RSS
- Linux: Falls back to `getrusage` peak RSS (no easy current RSS without /proc)
- Both platforms report in KB for consistency

## See Also

- [docs/event_loop_architecture.md](event_loop_architecture.md) - EventLoop design
- [docs/EVENT_LOOP_SHUTDOWN.md](EVENT_LOOP_SHUTDOWN.md) - Lifecycle and shutdown contract
- [docs/THREADING_MODEL.md](THREADING_MODEL.md) - Thread pool architecture
