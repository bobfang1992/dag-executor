# Request Lifecycle

This document traces through the runtime execution of a complex DAG plan using the async scheduler.

## Example Plan

```typescript
import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "complex_dag",
  build: (ctx) => {
    const v = ctx.viewer({ endpoint: EP.redis.redis_default });

    // Left branch: follow → media → vm
    const followBranch = v
      .follow({ endpoint: EP.redis.redis_default })
      .media({ endpoint: EP.redis.redis_default })
      .vm({ outKey: Key.score, expr: Key.id * coalesce(P.weight, 0.1) });

    // Right branch: recommendation → media → vm
    const recsBranch = v
      .recommendation({ endpoint: EP.redis.redis_default })
      .media({ endpoint: EP.redis.redis_default })
      .vm({ outKey: Key.score, expr: Key.id * coalesce(P.weight, 0.1) });

    // Merge, sort, take
    return followBranch
      .concat({ rhs: recsBranch })
      .sort({ key: Key.score, order: "desc" })
      .take({ count: 50 });
  },
});
```

## 1. Plan Compilation (Build Time)

```
TypeScript → dslc compiler → JSON artifact
```

The plan compiles to a DAG with 10 nodes:

```
         n0 (viewer)
           /     \
      n1(follow)  n2(recs)
          |          |
      n3(media)   n4(media)
          |          |
      n5(vm)      n6(vm)
           \      /
          n7(concat)
              |
          n8(sort)
              |
          n9(take)
```

## 2. Request Arrives

```bash
echo '{"user_id": 123}' | engine/bin/rankd --async_scheduler --plan_name complex_dag
```

**main.cpp** parses request, loads plan, initializes:
- `EventLoop` (single libuv thread)
- `AsyncIoClients` (Redis connection pool)
- `GetCPUThreadPool()` (8 threads for vm/sort/etc)

## 3. Async Scheduler Initialization

**`execute_plan_async_blocking()`** builds scheduler state:

```cpp
struct AsyncSchedulerState {
  deps_remaining[10];    // [0,1,1,1,1,1,1,2,1,1] - n7 needs 2 inputs
  results[10];           // Empty slots for RowSets
  ready_queue;           // Initially [n0] - viewer has no deps
  inflight_count = 0;
  first_error = nullopt;
};
```

## 4. Node Execution Loop

### Wave 1: n0 (viewer)

```
┌─────────────────────────────────────────────────────────────┐
│  Loop Thread                                                 │
├─────────────────────────────────────────────────────────────┤
│  spawn_ready_nodes() → sees n0 ready                        │
│  run_node_async(n0) starts as coroutine                     │
│                                                              │
│  n0 has run_async (Redis HGETALL user:123)                  │
│  co_await AsyncRedisClient::HGetAll(...)                    │
│  └─► Coroutine SUSPENDS, hiredis sends command              │
│                                                              │
│  Loop polls for IO...                                        │
│                                                              │
│  Redis reply arrives!                                        │
│  └─► Coroutine RESUMES with {user_id:123, country:"US"}     │
│                                                              │
│  n0 completes → results[0] = RowSet(1 row)                  │
│  deps_remaining[n1]-- → 0 (ready!)                          │
│  deps_remaining[n2]-- → 0 (ready!)                          │
│  ready_queue = [n1, n2]                                      │
└─────────────────────────────────────────────────────────────┘
```

### Wave 2: n1 (follow) + n2 (recs) - CONCURRENT

```
┌─────────────────────────────────────────────────────────────┐
│  Loop Thread                                                 │
├─────────────────────────────────────────────────────────────┤
│  spawn_ready_nodes() → spawns n1 AND n2                     │
│                                                              │
│  run_node_async(n1):                                         │
│    co_await Redis LRANGE follow:123 → SUSPENDS              │
│                                                              │
│  run_node_async(n2):                                         │
│    co_await Redis LRANGE recs:123 → SUSPENDS                │
│                                                              │
│  Both coroutines suspended, both Redis commands in-flight!  │
│  Loop polls... (handles both replies as they arrive)        │
│                                                              │
│  n1 reply: [101,102,103,104] → results[1] = RowSet(4 rows) │
│  n2 reply: [201,202,203,204] → results[2] = RowSet(4 rows) │
│                                                              │
│  deps_remaining[n3]-- → 0, deps_remaining[n4]-- → 0         │
│  ready_queue = [n3, n4]                                      │
└─────────────────────────────────────────────────────────────┘
```

### Wave 3: n3 (media) + n4 (media) - CONCURRENT

Same pattern - both spawn, both do Redis LRANGE, both suspend, both complete.

### Wave 4: n5 (vm) + n6 (vm) - CPU OFFLOAD

```
┌─────────────────────────────────────────────────────────────┐
│  Loop Thread                          CPU Pool               │
├─────────────────────────────────────────────────────────────┤
│  run_node_async(n5):                                         │
│    vm has NO run_async → wrap with OffloadCpu               │
│    co_await OffloadCpu([&]() {                              │
│      // Runs on CPU thread ─────────► [T1] eval expr        │
│      return registry.execute(...)    │      Key.id * 0.1    │
│    }) ← SUSPENDS                     │                      │
│                                       │                      │
│  run_node_async(n6):                  │                      │
│    co_await OffloadCpu([&]() { ─────► [T2] eval expr        │
│      ...                              │      Key.id * 0.1    │
│    }) ← SUSPENDS                      │                      │
│                                       │                      │
│  Loop polls...                        ▼                      │
│                                  T1 done: Post(resume n5)   │
│  ◄─────────────────────────────────────                     │
│  n5 resumes, completes                                       │
│                                  T2 done: Post(resume n6)   │
│  ◄─────────────────────────────────────                     │
│  n6 resumes, completes                                       │
│                                                              │
│  deps_remaining[n7]-- → 1                                   │
│  deps_remaining[n7]-- → 0 (ready!)                          │
└─────────────────────────────────────────────────────────────┘
```

### Wave 5: n7 (concat) - CPU

```cpp
// concat merges left + right branch results
// Input: results[5] (4 rows) + results[6] (4 rows via NodeRef)
// Output: RowSet(8 rows)
```

### Wave 6: n8 (sort) - CPU

```cpp
// sort by Key.score DESC
// Updates PermutationVector, no data copy
// Output: RowSet(8 rows, sorted)
```

### Wave 7: n9 (take) - CPU

```cpp
// take({ count: 50 })
// Only 8 rows exist, so all pass through
// Updates SelectionVector, no data copy
// Output: RowSet(8 rows)
```

## 5. Completion

```
┌─────────────────────────────────────────────────────────────┐
│  All nodes complete:                                         │
│    inflight_count → 0                                        │
│    main_coro.resume() via Post()                            │
│                                                              │
│  execute_plan_async() returns ExecutionResult:              │
│    results: [n9's RowSet]                                   │
│    schema_deltas: [...]                                      │
│    success: true                                             │
└─────────────────────────────────────────────────────────────┘
```

## 6. Response

```json
{
  "request_id": "...",
  "candidates": [
    {"id": 203, "score": 20.3},
    {"id": 204, "score": 20.4},
    ...
  ]
}
```

## Timeline Visualization

```
Time →
     0ms     10ms    20ms    30ms    40ms    50ms
      │       │       │       │       │       │
n0    ████░░░░│       │       │       │       │  Redis HGETALL
      │       │       │       │       │       │
n1    │       ████░░░░│       │       │       │  Redis LRANGE (concurrent)
n2    │       ████░░░░│       │       │       │  Redis LRANGE (concurrent)
      │       │       │       │       │       │
n3    │       │       ████░░░░│       │       │  Redis LRANGE (concurrent)
n4    │       │       ████░░░░│       │       │  Redis LRANGE (concurrent)
      │       │       │       │       │       │
n5    │       │       │       ██░░░░░░│       │  CPU vm (concurrent)
n6    │       │       │       ██░░░░░░│       │  CPU vm (concurrent)
      │       │       │       │       │       │
n7    │       │       │       │       █░░░░░░│  CPU concat
n8    │       │       │       │       █░░░░░░│  CPU sort
n9    │       │       │       │       █░░░░░░│  CPU take

████ = Async IO (Redis)
██ = CPU work
░░ = Waiting/suspended
```

## Key Points

1. **Parallelism**: Independent branches (n1/n2, n3/n4, n5/n6) run concurrently
2. **No blocking**: Redis calls suspend coroutines, loop handles other work
3. **CPU offload**: vm/sort/take run on thread pool, post back when done
4. **Zero-copy**: sort/take update vectors, don't copy data
5. **Single loop thread**: All scheduler state mutations happen here (no locks)

## With Timeout/Deadline

If `--deadline_ms 30` is set:

```
┌─────────────────────────────────────────────────────────────┐
│  Time 0ms: Request starts, deadline = now + 30ms            │
│                                                              │
│  Time 25ms: n5 (vm) starts                                  │
│    effective_deadline = min(request_deadline, node_timeout) │
│    co_await OffloadCpuWithTimeout(deadline, [...])          │
│                                                              │
│  Time 30ms: Deadline reached!                               │
│    Timer fires → sets done=true → resumes with timeout error│
│    CPU work continues in background (late completion)       │
│                                                              │
│  Response: {"error": "Node execution timeout"}              │
│                                                              │
│  Time 35ms: CPU work finishes (late completion)             │
│    Checks done flag → already true → discards result        │
│    Increments late_counter, cleans up                       │
└─────────────────────────────────────────────────────────────┘
```

## Related Documentation

- [async_dag_scheduler_architecture.md](async_dag_scheduler_architecture.md) - Scheduler internals
- [EVENT_LOOP_SHUTDOWN.md](EVENT_LOOP_SHUTDOWN.md) - Shutdown/drain semantics
- [THREADING_MODEL.md](THREADING_MODEL.md) - Thread pool architecture
