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
| `engine/include/cpu_offload.h` | OffloadCpu awaitable |
| `engine/include/async_dag_scheduler.h` | ExecCtxAsync, scheduler declarations |
| `engine/src/async_dag_scheduler.cpp` | Scheduler implementation |
| `engine/include/task_registry.h` | AsyncTaskFn, run_async field |
| `engine/src/tasks/*.cpp` | Task run_async implementations |
| `engine/tests/test_dag_scheduler.cpp` | Async scheduler tests |

## Testing

### Async Scheduler Tests

Located in `engine/tests/test_dag_scheduler.cpp`:

| Test | Purpose |
|------|---------|
| `async scheduler: three-branch DAG with concurrent sleep + vm` | Verifies concurrent execution of independent branches (sleep_a, sleep_b, vm) |
| `async scheduler: fault injection - no deadlock or UAF on error` | Verifies fail-fast + safe teardown (waits for all in-flight tasks) |

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

# Only async scheduler tests
engine/bin/dag_scheduler_tests "[async_scheduler]"

# Only concurrent test
engine/bin/dag_scheduler_tests "[async_scheduler][concurrent]"

# Only fault injection test
engine/bin/dag_scheduler_tests "[async_scheduler][fault_injection]"
```
