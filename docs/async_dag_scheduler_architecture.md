# Async DAG Scheduler Architecture (Step 14.5c.3)

This document describes the coroutine-based DAG scheduler that runs on a single libuv event loop thread.

## Overview

The async DAG scheduler replaces the multi-threaded DAG scheduler with a coroutine-based approach:

- **Single libuv thread** drives all IO and scheduler logic
- **CPU thread pool** handles compute-intensive tasks (vm, filter, sort)
- **Coroutines** (`Task<RowSet>`) suspend on IO, resume when complete
- **No locks** in scheduler state (all mutations on loop thread)

## Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                     Async DAG Scheduler                               │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐ │
│  │                    EventLoop (single thread)                     │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │ │
│  │  │ libuv poll  │  │ Post queue  │  │ Scheduler state         │  │ │
│  │  │ (Redis IO)  │  │ (wake-ups)  │  │ (in_degree, outputs)    │  │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────────────┘  │ │
│  └─────────────────────────────────────────────────────────────────┘ │
│                              │                                        │
│                              │ spawn coroutines                       │
│                              ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────┐ │
│  │               Suspended Coroutines (Task<RowSet>)                │ │
│  │                                                                   │ │
│  │   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐           │ │
│  │   │ viewer      │   │ follow      │   │ recommendation│          │ │
│  │   │ co_await    │   │ co_await    │   │ co_await      │          │ │
│  │   │ Redis       │   │ Redis       │   │ Redis         │          │ │
│  │   └─────────────┘   └─────────────┘   └─────────────┘           │ │
│  │                                                                   │ │
│  │   ┌─────────────┐   ┌─────────────┐                              │ │
│  │   │ vm          │   │ filter      │                              │ │
│  │   │ co_await    │   │ co_await    │   (CPU tasks wrapped in     │ │
│  │   │ OffloadCpu  │   │ OffloadCpu  │    OffloadCpu awaitable)    │ │
│  │   └─────────────┘   └─────────────┘                              │ │
│  └─────────────────────────────────────────────────────────────────┘ │
│                              │                                        │
│                              │ OffloadCpu                             │
│                              ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────┐ │
│  │                     CPU Thread Pool                              │ │
│  │   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐               │ │
│  │   │ Thread 1│ │ Thread 2│ │ Thread 3│ │ Thread N│               │ │
│  │   │ vm work │ │ filter  │ │ sort    │ │         │               │ │
│  │   └─────────┘ └─────────┘ └─────────┘ └─────────┘               │ │
│  │                                                                   │ │
│  │   When complete: loop.Post([h]() { h.resume(); })               │ │
│  └─────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

## Key Components

### 1. ExecCtxAsync

Execution context for async tasks, passed to `run_async`:

```cpp
struct ExecCtxAsync {
  const ParamTable* params;
  const std::unordered_map<std::string, ExprNodePtr>* expr_table;
  const std::unordered_map<std::string, PredNodePtr>* pred_table;
  ExecStats* stats;
  const std::unordered_map<std::string, RowSet>* resolved_node_refs;
  const RequestContext* request;
  const EndpointRegistry* endpoints;
  EventLoop* loop;                    // For async operations
  AsyncIoClients* async_clients;      // Process-level Redis clients
};
```

### 2. AsyncTaskFn

New task signature for async execution:

```cpp
using AsyncTaskFn = std::function<Task<RowSet>(
    const std::vector<RowSet>&,
    const ValidatedParams&,
    const ExecCtxAsync&
)>;
```

### 3. OffloadCpu Awaitable

Runs CPU-intensive work on thread pool, resumes on event loop:

```cpp
template <typename F>
class OffloadCpu {
public:
  OffloadCpu(EventLoop& loop, F&& fn);

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) {
    GetCPUThreadPool().submit([this, h]() {
      // Run on CPU thread
      result_ = fn_();
      // Resume on loop thread
      loop_.Post([h]() { h.resume(); });
    });
  }

  ResultType await_resume() { return std::move(result_); }
};
```

### 4. AsyncSchedulerState

Lock-free scheduler state (all access on loop thread):

```cpp
struct AsyncSchedulerState {
  const Plan& plan;
  ExecCtxAsync ctx;
  std::unordered_map<std::string, int> in_degree;      // Remaining deps
  std::unordered_map<std::string, RowSet> outputs;     // Completed outputs
  size_t completed_count{0};
  size_t total_nodes{0};
  std::exception_ptr error;
  std::coroutine_handle<> completion_handle;           // Resume when done
};
```

## Execution Flow

### 1. Plan Submission

```cpp
Task<ExecutionResult> execute_plan_async(const Plan& plan, const ExecCtxAsync& ctx) {
  AsyncSchedulerState state{plan, ctx, ...};

  // Initialize in-degree for all nodes
  for (const auto& node : plan.nodes) {
    state.in_degree[node.node_id] = node.inputs.size();
  }

  // Start all nodes with no dependencies
  for (const auto& node : plan.nodes) {
    if (state.in_degree[node.node_id] == 0) {
      spawn_node(state, node);  // Fire-and-forget coroutine
    }
  }

  // Wait for all nodes to complete
  co_await CompletionAwaitable{state};

  co_return build_result(state);
}
```

### 2. Node Execution

```cpp
Task<void> run_node_async(AsyncSchedulerState& state, const Node& node) {
  // Gather inputs
  std::vector<RowSet> inputs = gather_inputs(state, node);

  // Execute task
  RowSet result;
  const auto& spec = TaskRegistry::get(node.op);

  if (spec.run_async) {
    // Native async implementation
    result = co_await spec.run_async(inputs, params, state.ctx);
  } else {
    // Wrap sync task with OffloadCpu
    result = co_await OffloadCpu(*state.ctx.loop, [&]() {
      return spec.run(inputs, params, sync_ctx);
    });
  }

  // Store result and notify dependents
  state.outputs[node.node_id] = std::move(result);
  notify_dependents(state, node);
}
```

### 3. Dependency Resolution

When a node completes:

```cpp
void notify_dependents(AsyncSchedulerState& state, const Node& node) {
  for (const auto& dependent : get_dependents(node)) {
    if (--state.in_degree[dependent.node_id] == 0) {
      spawn_node(state, dependent);  // Ready to run
    }
  }

  if (++state.completed_count == state.total_nodes) {
    // All done - resume completion awaitable
    state.completion_handle.resume();
  }
}
```

## Task Migration

### Native Async Tasks

IO tasks implement `run_async` directly:

```cpp
// viewer.cpp
static Task<RowSet> run_async(const std::vector<RowSet>& inputs,
                               const ValidatedParams& params,
                               const ExecCtxAsync& ctx) {
  auto& redis = *ctx.async_clients->GetRedis(*ctx.loop, *ctx.endpoints, endpoint_id);

  // Non-blocking Redis call
  auto result = co_await redis.HGetAll(key);

  co_return build_rowset(result);
}
```

### CPU Tasks (Default Wrapper)

Tasks without `run_async` are automatically wrapped:

```cpp
if (!spec.run_async) {
  result = co_await OffloadCpu(*ctx.loop, [&]() {
    return spec.run(inputs, params, sync_ctx);
  });
}
```

## Error Handling and Shutdown

### Fail-Fast with Safe Teardown

The scheduler uses fail-fast semantics: when any node fails, no new nodes are spawned. However, we must wait for all in-flight tasks to complete before destroying state.

**Key invariants:**
1. `inflight_count` tracks running coroutines (incremented before spawn, decremented after completion)
2. `main_coro` is only resumed when `inflight_count == 0`
3. Resume is posted async to avoid destroying running coroutine

```cpp
// In run_node_async, after on_node_success or on_node_failure:
--state.inflight_count;
if (state.inflight_count == 0 && state.main_coro) {
  auto main_coro = state.main_coro;
  // IMPORTANT: Post async to let this coroutine complete before state destruction
  state.base_ctx.loop->Post([main_coro]() { main_coro.resume(); });
}
```

### Regex Cache on CPU Threads

Thread-local regex cache must be cleared on CPU threads before sync task execution to avoid stale entries:

```cpp
co_return co_await OffloadCpu(*ctx.loop, [&]() {
  rankd::clearRegexCache();  // Clear on CPU thread, not loop thread
  return state.registry.execute(node.op, inputs, validated, sync_ctx);
});
```

## Deadline and Timeout (Step 14.5c.5b)

### Overview

The scheduler supports request-level deadlines and per-node timeouts:

- `--deadline_ms N`: Absolute deadline from request start
- `--node_timeout_ms N`: Maximum time per node

### OffloadCpuWithTimeout

For CPU tasks, `OffloadCpuWithTimeout` implements a **first-wins** pattern between timer and CPU completion:

```
┌─────────────────────────────────────────────────────────────────┐
│                    OffloadCpuWithTimeout                        │
├─────────────────────────────────────────────────────────────────┤
│  shared State {                                                 │
│    bool completed = false;  // First-wins guard                 │
│    variant<Result, exception_ptr> result;                       │
│  }                                                              │
├─────────────────────────────────────────────────────────────────┤
│  CPU Thread              Timer (loop thread)                    │
│  ──────────              ───────────────────                    │
│  Execute fn()            Start uv_timer                         │
│       │                       │                                 │
│       │                       ▼                                 │
│       │                  Timer fires                            │
│       │                  if (!completed) {                      │
│       │                    completed = true ◄── WINS            │
│       │                    result = timeout_error               │
│       │                    handle.resume()                      │
│       │                  }                                      │
│       ▼                                                         │
│  fn() returns                                                   │
│  Post to loop:                                                  │
│    if (!completed) ──► FALSE (timer already won)                │
│      return;  // Discard result                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Key Design Points

1. **Timeout, NOT Cancellation**: CPU work runs to completion; result is discarded on timeout
2. **All state on loop thread**: Timer callback and CPU Post callback both run on loop thread
3. **Capture-by-value**: CPU lambda owns `inputs`, `validated`, `op` to avoid use-after-free

### AsyncWithTimeout (Step 14.5c.5c)

For async tasks (those with `run_async`, like `viewer`, `follow`, `sleep`, Redis-backed tasks), `AsyncWithTimeout` implements timeout using a **detached runner coroutine**:

```
┌─────────────────────────────────────────────────────────────────┐
│                      AsyncWithTimeout                            │
├─────────────────────────────────────────────────────────────────┤
│  shared State {                                                  │
│    bool done = false;           // First-wins guard              │
│    variant<Result, exception_ptr> result;                        │
│    optional<Task<void>> runner; // Keeps coroutine frame alive   │
│    bool await_suspend_returned; // Safe Post() fallback          │
│  }                                                               │
├─────────────────────────────────────────────────────────────────┤
│  Runner Coroutine (loop thread)    Timer (loop thread)           │
│  ─────────────────────────────     ───────────────────           │
│  co_await inner_task               Start uv_timer                │
│       │                                 │                        │
│       │                                 ▼                        │
│       │                            Timer fires                   │
│       │                            if (!done) {                  │
│       │                              done = true ◄── WINS        │
│       │                              result = timeout_error      │
│       │                              Post(resume)                │
│       │                            }                             │
│       ▼                                                          │
│  inner_task completes                                            │
│  if (!done) {                                                    │
│    done = true  ◄── WINS                                         │
│    result = success/error                                        │
│    cancel_timer()                                                │
│    Post(resume)                                                  │
│  } else {                                                        │
│    late_counter++ // Discard result                              │
│  }                                                               │
│                                                                  │
│  RunnerCleanup posts: runner = nullopt (breaks cycle)            │
└─────────────────────────────────────────────────────────────────┘
```

**Key design points:**

1. **Runner stored in State**: The detached runner coroutine is stored in `State::runner` to keep its frame alive until completion. Without this, the Task destructor destroys the frame, causing SIGSEGV.

2. **Reference cycle breaking**: `State → runner → frame → state → State` cycle is broken by `RunnerCleanup` RAII guard that posts cleanup after the runner reaches `final_suspend`.

3. **All resumes via Post()**: Runner completion paths use `loop.Post()` to resume the waiter, ensuring we never resume inside `await_suspend` (which would violate the coroutine contract).

4. **await_suspend_returned flag**: Tracks when `await_suspend` has returned, enabling safe direct resume fallback if `Post()` fails during shutdown.

5. **Copy ALL ctx data**: The wrapper coroutine captures `shared_ptr` copies of all data (params, expr_table, pred_table, request, endpoints, resolved_refs) to prevent UAF when timeout fires and `run_node_async` exits.

**Late completion**: When timeout wins, the async task continues in the runner until it completes, but the result is discarded. A `LateCompletionCounter` test hook verifies this behavior.

### Safety Mechanisms

**Safe capture**: When timeout fires, the coroutine frame is destroyed while CPU work continues. To prevent use-after-free:
- All ExecCtx data (params, expr_table, pred_table, request, endpoints) is copied into `shared_ptr`
- The CPU lambda captures these by value, owning the data
- `resolved_refs` also uses `shared_ptr` for NodeRef params (concat-style nodes)

**Drain semantics**: Before destroying EventLoop, we must wait for pending CPU jobs:
- `ThreadPool::wait_idle()` blocks until all in-flight tasks complete
- Called after `loop.Stop()` and on exception (inner try-catch with rethrow)
- Prevents CPU jobs from calling `loop->Post()` on destroyed EventLoop

### Deadline Checks

Deadlines are checked at two points:

```cpp
// 1. Before spawning new nodes
void spawn_ready_nodes(AsyncSchedulerState& state) {
  if (deadline_exceeded(state.request_deadline)) {
    state.first_error = "Request deadline exceeded";
    return;  // Don't spawn
  }
  // ...
}

// 2. At node start
Task<void> run_node_async(...) {
  auto effective = compute_effective_deadline(now, request_deadline, node_timeout);
  if (deadline_exceeded(effective)) {
    throw std::runtime_error("Deadline exceeded before node start");
  }
  // ...
}
```

### Effective Deadline

Per-node deadline is the minimum of request deadline and node timeout:

```cpp
inline OptionalDeadline compute_effective_deadline(
    SteadyTimePoint start_time,
    OptionalDeadline request_deadline,
    std::optional<std::chrono::milliseconds> node_timeout) {
  OptionalDeadline effective = request_deadline;
  if (node_timeout) {
    auto node_deadline = start_time + *node_timeout;
    if (!effective || node_deadline < *effective) {
      effective = node_deadline;
    }
  }
  return effective;
}
```

### Usage

```bash
# Request must complete within 100ms
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --deadline_ms 100

# Each node has max 50ms
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --node_timeout_ms 50

# Both: request deadline 200ms, each node max 50ms
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --deadline_ms 200 --node_timeout_ms 50
```

## Thread Safety

| Component | Thread | Synchronization |
|-----------|--------|-----------------|
| AsyncSchedulerState | Loop thread only | None needed |
| AsyncRedisClient | Loop thread only | None needed |
| AsyncIoClients | Loop thread only | None needed |
| CPU Thread Pool | Worker threads | Internal queue locks |
| EventLoop.Post() | Any thread | Lock-free queue |

## Concurrency Model

### Level 1: Request Concurrency
- Multiple requests via `--bench_concurrency`
- Each request gets its own coroutine tree
- Shared: EventLoop, AsyncIoClients, CPU pool

### Level 2: Within-Request Parallelism
- Independent DAG branches run concurrently
- IO tasks suspend, allowing others to proceed
- CPU tasks offloaded to thread pool

Example DAG execution timeline:

```
Time →
      ┌──────────────┐
      │   viewer     │ Redis HGetAll
      └──────┬───────┘
             │
    ┌────────┴────────┐
    ▼                 ▼
┌────────┐       ┌────────┐
│ follow │       │ recs   │  Both run concurrently
│ Redis  │       │ Redis  │
└────┬───┘       └────┬───┘
     │                │
     ▼                ▼
┌────────┐       ┌────────┐
│ vm     │       │ vm     │  CPU pool (parallel)
└────┬───┘       └────┬───┘
     │                │
     └────────┬───────┘
              ▼
         ┌────────┐
         │ concat │
         └────────┘
```

## Usage

```bash
# Enable async scheduler
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --plan_name my_plan

# Benchmark mode with async scheduler
engine/bin/rankd --async_scheduler --bench 1000 --bench_concurrency 100 --plan_name my_plan
```

## Benefits

1. **Scalable IO**: 1 thread handles 100+ concurrent Redis calls
2. **No thread-per-request**: Coroutines are cheap (stack frames only)
3. **Natural backpressure**: Suspended coroutines don't consume threads
4. **Simple mental model**: All scheduler logic on one thread

## Files

| File | Purpose |
|------|---------|
| `engine/include/cpu_offload.h` | OffloadCpu, OffloadCpuWithTimeout, AsyncWithTimeout awaitables |
| `engine/include/deadline.h` | Deadline types and helpers |
| `engine/include/async_dag_scheduler.h` | ExecCtxAsync, scheduler declarations |
| `engine/src/async_dag_scheduler.cpp` | Scheduler implementation |
| `engine/include/task_registry.h` | AsyncTaskFn, run_async field |
| `engine/src/tasks/*.cpp` | Task run_async implementations |
| `engine/src/tasks/fixed_source.cpp` | Pure source task (CI testing, no Redis) |
| `engine/src/tasks/busy_cpu.cpp` | CPU spin task (timeout testing) |
| `engine/tests/test_dag_scheduler.cpp` | Async scheduler tests |

## Testing

### Async Scheduler Tests

Located in `engine/tests/test_dag_scheduler.cpp`:

| Test | Purpose |
|------|---------|
| `three-branch DAG with concurrent sleep + vm` | Concurrent execution of independent branches |
| `fault injection - no deadlock or UAF on error` | Fail-fast + safe teardown |
| `request deadline exceeded` | Request-level deadline fires |
| `node timeout on CPU work` | Per-node timeout fires |
| `deadline already expired` | Deadline in the past fails immediately |
| `very short deadline (1ms)` | Minimal deadline quick timeout |
| `generous deadline succeeds` | Ample time completes successfully |
| `very short node timeout (1ms)` | Minimal node timeout |
| `generous node timeout succeeds` | Ample timeout completes |
| `both deadline and node_timeout set` | Node timeout fires first |
| `multi-stage pipeline timeout` | 2-stage pipeline timing out |
| `multi-stage pipeline succeeds` | 2-stage with generous deadline |
| `fixed_source only (no CPU offload)` | Async path without OffloadCpu |
| `repeated timeout operations` | 5 iterations (leak check) |
| `repeated success operations` | 10 iterations (stability) |
| `alternating success and timeout` | 6 iterations mixed |
| `sleep respects request deadline` | Async task timeout via AsyncWithTimeout |
| `sleep respects node timeout` | Async task node timeout |
| `late completion increments counter` | Verifies late completion handling |
| `mixed async+CPU pipeline timeout (async)` | Timeout in async phase |
| `mixed async+CPU pipeline timeout (CPU)` | Timeout in CPU phase |
| `mixed async+CPU pipeline succeeds` | Generous deadline passes |
| `parallel async tasks both respect deadline` | Multiple concurrent async timeouts |

### Fault Injection

The `sleep` task supports fault injection for testing error handling:

```cpp
// In plan JSON:
{
  "node_id": "failing_node",
  "op": "sleep",
  "params": {
    "duration_ms": 20,
    "fail_after_sleep": true  // Throws after sleeping
  }
}
```

### Running Tests

```bash
# All DAG scheduler tests (sync + async)
engine/bin/dag_scheduler_tests

# Only async scheduler tests (28 tests, 138 assertions)
engine/bin/dag_scheduler_tests "[async_scheduler]"

# Deadline/timeout tests
engine/bin/dag_scheduler_tests "*deadline*"
engine/bin/dag_scheduler_tests "*timeout*"

# Stress tests
engine/bin/dag_scheduler_tests "*repeated*"
engine/bin/dag_scheduler_tests "*alternating*"
```
