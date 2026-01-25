# Threading Model

This document describes the threading architecture for the DAG executor engine.

## Overview

The engine uses a **two-pool architecture** to separate blocking IO operations from CPU-bound compute work. This prevents IO-bound tasks from starving compute tasks.

```
┌─────────────────────────────────────────────────────────────┐
│                      Main Thread                             │
│  - Request parsing                                           │
│  - Plan loading/validation                                   │
│  - DAG scheduler loop (dispatches to pools)                  │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
┌─────────────────────────┐     ┌─────────────────────────┐
│      CPU Pool           │     │       IO Pool           │
│   (8 threads default)   │     │     (4 threads)         │
├─────────────────────────┤     ├─────────────────────────┤
│ • vm (expression eval)  │     │ • viewer (Redis HGET)   │
│ • filter (predicate)    │     │ • follow (Redis LRANGE) │
│ • sort (permutation)    │     │ • media (Redis LRANGE)  │
│ • take (truncation)     │     │ • recommendation        │
│ • concat (merge)        │     │   (Redis LRANGE)        │
│ • sleep (testing)       │     │                         │
└─────────────────────────┘     └─────────────────────────┘
```

## Thread Pools

### CPU Pool (`GetCPUThreadPool()`)

- **Purpose**: Execute CPU-bound compute tasks
- **Default size**: 8 threads (configurable via `--cpu_threads`)
- **Initialization**: `InitCPUThreadPool(n)` must be called before use
- **Tasks**: vm, filter, sort, take, concat, sleep

### IO Pool (`GetIOThreadPool()`)

- **Purpose**: Execute blocking IO tasks (Redis calls)
- **Size**: 4 threads (fixed)
- **Initialization**: Lazy singleton
- **Tasks**: viewer, follow, media, recommendation

## Task Classification

Tasks declare their pool affinity via the `is_io` field in `TaskSpec`:

```cpp
struct TaskSpec {
  // ...
  bool is_io = false;  // True for tasks that do blocking IO
};
```

Example from `viewer.cpp`:
```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "viewer",
    // ...
    .is_io = true,  // Redis HGETALL
  };
}
```

## DAG Scheduler

The parallel scheduler (`execute_plan_parallel()`) dispatches nodes based on `is_io`:

```cpp
// In dag_scheduler.cpp
const auto& spec = registry.get_spec(node.op);
if (spec.is_io) {
  GetIOThreadPool().submit([...] { run_node_job(...); });
} else {
  cpu_pool.submit([...] { run_node_job(...); });
}
```

### Scheduler Algorithm

1. **Initialization**: Build dependency graph using Kahn's algorithm
2. **Ready Queue**: Nodes with zero dependencies are added to ready queue
3. **Dispatch Loop**:
   - Pop node from ready queue
   - Check `spec.is_io` to select pool
   - Submit job to appropriate pool
4. **Completion**: When node completes, decrement successor deps
5. **Fail-Fast**: First error stops scheduling, waits for inflight to drain

### Thread Safety

| Component | Protection | Notes |
|-----------|------------|-------|
| `RedisClient` | `std::mutex` | Serializes all hiredis calls |
| `IoClients` | `std::mutex` | Protects client cache map |
| `ready_queue` | `std::mutex` + CV | Scheduler state |
| `deps_remaining` | `std::atomic<int>` | Per-node countdown |
| `results` | Lock on write | Node results stored after completion |
| Regex cache | `thread_local` | Cleared at start of each node job |

## Configuration

### CLI Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--cpu_threads` | 8 | Number of CPU pool threads |
| `--within_request_parallelism` | false | Enable parallel DAG execution |

### Benchmark Mode

```bash
# Single-threaded benchmark (sequential within request)
./bin/rankd --plan_name my_plan --bench 100

# Concurrent benchmark (multiple requests in parallel)
./bin/rankd --plan_name my_plan --bench 100 --bench_concurrency 4
```

Benchmark mode always enables within-request parallelism.

## Why Two Pools?

**Problem with single pool:**
- Redis calls block threads (~1-10ms network latency)
- If all threads are blocked on IO, no CPU work can proceed
- Head-of-line blocking, CPU underutilization

**Two pools solve this:**
- IO threads can block on Redis without affecting CPU pool
- CPU pool stays available for compute-intensive tasks
- Better resource utilization under mixed workloads

**Example:** Plan with `viewer → follow → vm → filter → take`
- `viewer`, `follow` run on IO pool (block on Redis)
- `vm`, `filter`, `take` run on CPU pool (pure compute)
- If 8 concurrent requests all hit Redis, CPU pool still has 8 threads for compute

## Future: Async Coroutines (14.5c)

The two-pool model is an interim solution. Step 14.5c will replace it with:

- **libuv event loop** for async IO (single thread)
- **C++20 coroutines** for suspend/resume at IO boundaries
- Fine-grained yielding: tasks can yield mid-execution on IO

This enables higher concurrency with fewer threads (1 thread can have 100+ Redis calls in flight vs 8 threads = max 8 calls with blocking IO).

### Progress

| Step | Status | Description |
|------|--------|-------------|
| 14.5c.1 | ✅ Done | EventLoop + Task<T> + SleepMs awaitable |
| 14.5c.2 | ✅ Done | Async Redis awaitable (hiredis async) |
| 14.5c.3 | ✅ Done | Coroutine DAG scheduler integration |

See [event_loop_architecture.md](event_loop_architecture.md) for detailed architecture diagrams.

See [EVENT_LOOP_SHUTDOWN.md](EVENT_LOOP_SHUTDOWN.md) for shutdown/drain semantics.
