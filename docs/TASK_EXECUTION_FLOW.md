# Task Execution Flow

This document explains how tasks flow through the async DAG scheduler, from JSON parsing to result output.

## Overview

```
JSON Plan → Parse → Validate → Build DAG → Execute Nodes → Collect Results
```

## Example 1: Single Node (Source Task)

### Plan (TypeScript)
```typescript
import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "simple_viewer",
  build: (ctx) => ctx.viewer({ endpoint: EP.redis.redis_default }),
});
```

### Execution Timeline
```
Time(ms)  Loop Thread              CPU Pool         Redis
────────────────────────────────────────────────────────────
   0      Parse plan
          Validate DAG
          viewer has 0 deps → spawn
          │
   1      run_node_async("v")
          │ has run_async? YES
          │ co_await redis.HGetAll()
          │ [suspended] ─────────────────────────────► HGETALL user:1
          │                                                   │
  15      │ ◄─────────────────────────────────────────────────┘
          [resumed]
          Store result
          All done → return
```

**Key points:**
- `viewer` has `run_async` → runs on loop thread
- Coroutine suspends during Redis call
- Loop thread is free to handle other work while waiting

---

## Example 2: Two Nodes (Source → CPU)

### Plan (TypeScript)
```typescript
import { definePlan, Pred, E } from "@ranking-dsl/runtime";

export default definePlan({
  name: "viewer_filter",
  build: (ctx) =>
    ctx
      .viewer({ endpoint: EP.redis.redis_default })
      .filter({ pred: Pred.cmp(">", E.key(Key.id), E.const(5)) }),
});
```

### Execution Timeline
```
Time(ms)  Loop Thread              CPU Pool         Redis
────────────────────────────────────────────────────────────
   0      viewer: 0 deps → spawn
          filter: 1 dep → wait
          │
   1      run_node_async("v")
          co_await redis.HGetAll() ───────────────────► HGETALL
          [suspended]                                       │
          │                                                 │
  15      ◄─────────────────────────────────────────────────┘
          [resumed]
          Store result["v"]
          filter deps: 1→0 → spawn
          │
  16      run_node_async("f")
          │ has run_async? NO
          │ co_await OffloadCpu ──────► filter work
          │ [suspended]                      │
          │                                  │ evaluate predicate
          │                                  │ update selection
  18      │ ◄────────────── Post(resume) ────┘
          [resumed]
          Store result["f"]
          All done → return
```

**Key points:**
- `filter` has no `run_async` → wrapped in `OffloadCpu`
- CPU work runs on thread pool, not loop thread
- Result posted back to loop thread for storage

---

## Example 3: Parallel Branches (Fan-out)

### Plan (TypeScript)
```typescript
import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "parallel_fanout",
  build: (ctx) => {
    const v = ctx.viewer({ endpoint: EP.redis.redis_default });
    const followBranch = v.follow({ endpoint: EP.redis.redis_default });
    const recsBranch = v.recommendation({ endpoint: EP.redis.redis_default });
    // Both branches returned - engine runs them in parallel
    return [followBranch, recsBranch];
  },
});
```

### DAG Structure
```
       v (viewer)
      / \
     /   \
 follow  recs
```

### Execution Timeline
```
Time(ms)  Loop Thread              CPU Pool         Redis
────────────────────────────────────────────────────────────
   0      v: 0 deps → spawn
          follow: 1 dep → wait
          recs: 1 dep → wait
          │
   1      run_node_async("v")
          co_await redis ─────────────────────────────► HGETALL
          [suspended]                                       │
  10      ◄─────────────────────────────────────────────────┘
          Store result["v"]
          follow: 1→0 → spawn
          recs: 1→0 → spawn
          │
  11      run_node_async("follow")        run_node_async("recs")
          co_await redis ──────────────────────────────► LRANGE follow:1
          [suspended]                                       │
          co_await redis ──────────────────────────────► LRANGE recs:1
          [suspended]                                       │
          │                                                 │
  25      ◄─────────────────────────────────────────────────┘ (follow done)
          Store result["follow"]
          │
  30      ◄─────────────────────────────────────────────────┘ (recs done)
          Store result["recs"]
          All done → return
```

**Key points:**
- Both `follow` and `recs` spawn when `v` completes
- Both Redis calls in flight simultaneously
- Single loop thread handles both suspended coroutines
- Results arrive in any order

---

## Example 4: Mixed IO + CPU Pipeline

### Plan (TypeScript)
```typescript
import { definePlan, Pred, E } from "@ranking-dsl/runtime";

export default definePlan({
  name: "mixed_pipeline",
  build: (ctx) =>
    ctx
      .viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default })
      .vm({ outKey: Key.score, expr: Key.id * coalesce(P.weight, 0.5) })
      .filter({ pred: Pred.cmp(">=", E.key(Key.score), E.const(0.5)) })
      .sort({ key: Key.score, order: "desc" })
      .take({ count: 10 }),
});
```

### DAG Structure
```
v → f → vm1 → filter → sort → take
```

### Execution Timeline
```
Time(ms)  Loop Thread              CPU Pool              Redis
──────────────────────────────────────────────────────────────────
   0      v: spawn
   1      co_await redis ──────────────────────────────► HGETALL
          [suspended]
  10      [resumed], store v
          f: spawn
  11      co_await redis ──────────────────────────────► LRANGE
          [suspended]
  25      [resumed], store f
          vm1: spawn
  26      co_await OffloadCpu ────────► evaluate expr
          [suspended]                        │
  28      ◄─────────────────── Post ─────────┘
          store vm1
          filter: spawn
  29      co_await OffloadCpu ────────► eval predicate
          [suspended]                        │
  31      ◄─────────────────── Post ─────────┘
          store filter
          sort: spawn
  32      co_await OffloadCpu ────────► permutation
          [suspended]                        │
  33      ◄─────────────────── Post ─────────┘
          store sort
          take: spawn
  34      co_await OffloadCpu ────────► slice
          [suspended]                        │
  35      ◄─────────────────── Post ─────────┘
          store take
          All done → return

Total: ~35ms (10ms Redis + 15ms Redis + 4x2ms CPU)
```

---

## Example 5: Diamond Pattern

### Plan (TypeScript)
```typescript
import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "diamond",
  build: (ctx) => {
    const v = ctx.viewer({ endpoint: EP.redis.redis_default });
    const left = v.follow({ endpoint: EP.redis.redis_default });
    const right = v.recommendation({ endpoint: EP.redis.redis_default });
    return left.concat({ rhs: right }); // waits for both branches
  },
});
```

### DAG Structure
```
       v
      / \
   left  right
      \  /
      join
```

### Execution Timeline
```
Time(ms)  Loop Thread                            Redis
────────────────────────────────────────────────────────────
   0      v: spawn
   1      co_await redis ────────────────────► HGETALL
          [suspended]
  10      [resumed], store v
          left: spawn, right: spawn
          │
  11      run_node_async("left")
          co_await redis ────────────────────► LRANGE follow
          [suspended]
          run_node_async("right")
          co_await redis ────────────────────► LRANGE recs
          [suspended]
          │
          │ (both in flight concurrently)
          │
  22      ◄──────────────────────────────────── left done
          store left
          join: 2→1 deps (not ready yet)
          │
  28      ◄──────────────────────────────────── right done
          store right
          join: 1→0 deps → spawn
          │
  29      run_node_async("join")
          concat is CPU, co_await OffloadCpu
          [suspended]
  30      [resumed]
          store join
          All done → return
```

**Key points:**
- `join` waits for BOTH `left` and `right`
- Dependency count decrements: 2 → 1 → 0
- Only spawns when all inputs ready

---

## Example 6: Complex 10-Node DAG with Timeout

### Plan (TypeScript)
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

### DAG Structure
```
              v
            /   \
       follow    recs
          |        |
      media_f   media_r
          |        |
        vm_f     vm_r
           \     /
            merge
              |
            sort
              |
            take
```

### Execution with --deadline_ms=100

```
Time(ms)  Loop Thread              CPU Pool 1    CPU Pool 2    Redis
────────────────────────────────────────────────────────────────────────
   0      Check deadline: OK
          v: spawn
   1      co_await redis ─────────────────────────────────────► HGETALL
          [suspended]
  10      [resumed], store v
          Check deadline: OK
          follow: spawn, recs: spawn
          │
  11      co_await redis ─────────────────────────────────────► LRANGE follow
          [suspended]
          co_await redis ─────────────────────────────────────► LRANGE recs
          [suspended]
          │
  25      ◄─────────────────────────────────────────────────────follow done
          store follow
          Check deadline: OK
          media_f: spawn
  26      co_await redis ─────────────────────────────────────► LRANGE media
          [suspended]
          │
  30      ◄─────────────────────────────────────────────────────recs done
          store recs
          Check deadline: OK
          media_r: spawn
  31      co_await redis ─────────────────────────────────────► LRANGE media
          [suspended]
          │
  45      ◄─────────────────────────────────────────────────────media_f done
          store media_f
          vm_f: spawn
  46      co_await OffloadCpu ────► vm work
          [suspended]                   │
          │                             │
  50      ◄────────────── Post ─────────┘
          store vm_f
          merge: 2→1 (waiting for vm_r)
          │
  55      ◄─────────────────────────────────────────────────────media_r done
          store media_r
          vm_r: spawn
  56      co_await OffloadCpu ─────────────────► vm work
          [suspended]                                │
          │                                          │
  60      ◄──────────────────────────── Post ────────┘
          store vm_r
          merge: 1→0 → spawn
  61      co_await OffloadCpu ────► concat
          [suspended]                  │
  62      ◄────────────── Post ────────┘
          store merge
          sort: spawn
  63      co_await OffloadCpu ────► sort
          [suspended]                  │
  65      ◄────────────── Post ────────┘
          store sort
          take: spawn
  66      co_await OffloadCpu ────► take
          [suspended]                  │
  67      ◄────────────── Post ────────┘
          store take
          All done → return

Total: 67ms (within 100ms deadline ✓)
```

### Same DAG with --deadline_ms=50 (TIMEOUT)

```
Time(ms)  Loop Thread
────────────────────────────────────
   0      Check deadline: OK, v: spawn
  10      v done, follow/recs: spawn
  25      follow done, media_f: spawn
  30      recs done, media_r: spawn
  45      media_f done, vm_f: spawn
  46      co_await OffloadCpuWithTimeout(deadline=50ms)
          Start timer(4ms), submit CPU work
          [suspended]
          │
  50      ⚡ TIMER FIRES (deadline exceeded)
          │ state.completed = true
          │ result = timeout_error
          │ handle.resume()
          │
          [resumed with error]
          throw "Node execution timeout"
          │
          on_node_failure: first_error = "timeout"
          │
          (CPU work still running, will finish ~52ms, result discarded)
          │
          inflight→0, resume main_coro
          │
          throw runtime_error("Node execution timeout")

Total: 50ms (deadline exceeded)
```

---

## How Task Scheduling is Decided

The decision of whether a task runs on the **loop thread** or **CPU pool** is made by the **task author** (infra engineer implementing the C++ task), not the engine automatically.

### Task Author's Choice

When implementing a task in C++, the author decides whether to provide `run_async`:

```cpp
// In engine/src/tasks/viewer.cpp (IO-bound task)
TaskSpec ViewerTask::spec() {
  return TaskSpec{
    .op = "viewer",
    .params_schema = { /* ... */ },
    .is_io = true,           // Hint: this is an IO task
    .run_async = run_async,  // ✅ Provides async implementation
  };
}

// In engine/src/tasks/vm.cpp (CPU-bound task)
TaskSpec VmTask::spec() {
  return TaskSpec{
    .op = "vm",
    .params_schema = { /* ... */ },
    .is_io = false,          // Hint: this is a CPU task
    // run_async not set     // ❌ No async implementation
  };
}
```

### Engine's Automatic Behavior

The **engine** then automatically determines execution location based on whether `run_async` is provided:

| Task provides `run_async`? | What engine does | Runs on |
|---------------------------|------------------|---------|
| ✅ Yes | Calls `run_async()` directly | Loop thread |
| ❌ No | Wraps sync `run()` in `OffloadCpu` | CPU pool |

```cpp
// In async_dag_scheduler.cpp
Task<RowSet> run_node_async(const std::string& node_id) {
  auto& spec = TaskRegistry::instance().get_spec(op);

  if (spec.run_async) {
    // Task author provided async impl → run on loop thread
    co_return co_await spec.run_async(inputs, params, async_ctx);
  } else {
    // No async impl → wrap sync run() with OffloadCpu
    co_return co_await OffloadCpu(cpu_pool, [&] {
      return TaskRegistry::instance().execute(op, inputs, params, sync_ctx);
    });
  }
}
```

### Why This Design?

- **IO-bound tasks** (Redis, HTTP) should implement `run_async` to use `co_await` on the loop thread, allowing hundreds of concurrent IO operations with a single thread.
- **CPU-bound tasks** (vm, filter, sort) should NOT implement `run_async`, letting the engine automatically offload them to the CPU pool to avoid blocking the loop thread.

---

## Future Optimization: Inline Cheap CPU Tasks

### The Problem

Currently ALL CPU tasks are offloaded to the thread pool, even trivial ones:

```
Loop thread                    CPU Pool
    │                              │
    ├─── Post(vm work) ───────────►│
    │    [context switch]          │
    │                              ├── 2ms vm work
    │◄─── Post(result) ────────────┤
    │    [context switch]          │
    ▼                              ▼
```

For a 2ms `vm` operation, the round-trip overhead (~0.5-1ms) is 25-50% of the work itself.

This is especially wasteful for sequential CPU chains:

```typescript
.vm({ ... })      // offload, return, offload, return
.filter({ ... })  // offload, return, offload, return
.sort({ ... })    // offload, return, offload, return
.take({ ... })    // offload, return
```

That's 4 round-trips for what could be one batch of CPU work.

### Why We Offload Everything Today

1. **Safety**: Keeps loop thread responsive. A slow `filter` with complex regex on 10K rows would block all Redis callbacks.
2. **Simplicity**: Binary decision (has `run_async` or not) - no heuristics.
3. **Worst-case protection**: "Cheap" operations can be expensive with bad inputs.

### Potential Solutions

**Option A: Task-level `run_inline` flag**

Task author declares a task is always safe to run inline:

```cpp
TaskSpec{
  .op = "take",
  .run_inline = true,  // Always run on loop thread, never offload
};
```

Good for: `take`, `fixed_source`, trivial transforms.

**Option B: Runtime size threshold**

Decide at runtime based on input size:

```cpp
if (input.size() < kInlineThreshold && !spec.is_expensive) {
  co_return run_sync(inputs, params);  // Inline on loop thread
} else {
  co_return co_await OffloadCpu(...);  // Offload to pool
}
```

Requires tuning `kInlineThreshold` (maybe 100-500 rows?).

**Option C: Batch sequential CPU tasks**

Detect sequential CPU chains and batch them:

```
Before: vm → [offload] → filter → [offload] → sort → [offload] → take → [offload]
After:  vm+filter+sort+take → [single offload]
```

Requires scheduler to detect CPU chains and fuse them. More complex but biggest win.

**Option D: Hybrid - inline small, batch large chains**

Combine B + C:
- Small inputs (< 100 rows): run inline
- Large inputs with CPU chains: batch offload
- Large inputs single CPU task: individual offload

### Recommendation

Start with **Option A** (task-level `run_inline`) for quick wins:
- `take` is almost always trivial (slice a vector)
- `fixed_source` just returns static data

Then measure real workloads to decide if B/C/D are worth the complexity.

### Open Questions

1. What's the actual round-trip overhead? (Need benchmarks)
2. What input size threshold makes offloading worthwhile?
3. Should this be configurable per-request for experimentation?

---

## Task Type Summary

| Task | Has `run_async` | Runs On | Suspends For |
|------|-----------------|---------|--------------|
| viewer | ✅ | Loop thread | Redis HGETALL |
| follow | ✅ | Loop thread | Redis LRANGE |
| recommendation | ✅ | Loop thread | Redis LRANGE |
| media | ✅ | Loop thread | Redis LRANGE |
| vm | ❌ | CPU pool | OffloadCpu |
| filter | ❌ | CPU pool | OffloadCpu |
| sort | ❌ | CPU pool | OffloadCpu |
| take | ❌ | CPU pool | OffloadCpu |
| concat | ❌ | CPU pool | OffloadCpu |
| sleep | ✅ | Loop thread | uv_timer |
| fixed_source | ✅ | Loop thread | (none, instant) |
| busy_cpu | ❌ | CPU pool | OffloadCpu |

---

## Scheduler State Machine

```
┌─────────────────────────────────────────────────────────────────┐
│                     AsyncSchedulerState                          │
├─────────────────────────────────────────────────────────────────┤
│  deps_remaining[node] = count of unfinished parents             │
│  ready_queue = nodes with deps_remaining == 0                   │
│  results[node] = completed RowSet                               │
│  inflight_count = running coroutines                            │
│  first_error = first failure (fail-fast)                        │
└─────────────────────────────────────────────────────────────────┘

                    ┌──────────────┐
                    │  Initialize  │
                    │  deps_remaining │
                    └──────┬───────┘
                           │
                           ▼
              ┌────────────────────────┐
              │ Enqueue nodes with     │
              │ deps_remaining == 0    │
              └───────────┬────────────┘
                          │
           ┌──────────────┴──────────────┐
           ▼                             │
   ┌───────────────┐                     │
   │ spawn_ready   │◄────────────────────┤
   │ _nodes()      │                     │
   └───────┬───────┘                     │
           │                             │
           ▼                             │
   ┌───────────────┐                     │
   │run_node_async │                     │
   │  (coroutine)  │                     │
   └───────┬───────┘                     │
           │                             │
     ┌─────┴─────┐                       │
     ▼           ▼                       │
 [success]    [error]                    │
     │           │                       │
     ▼           ▼                       │
 on_node_    on_node_                    │
 success()   failure()                   │
     │           │                       │
     │           └──► first_error = msg  │
     │                                   │
     ▼                                   │
 for each successor:                     │
   deps_remaining[succ]--                │
   if deps_remaining == 0:               │
     ready_queue.push(succ) ─────────────┘
     │
     ▼
 spawn_ready_nodes() ◄───────────────────┘
     │
     ▼
 inflight_count--
 if inflight_count == 0:
   resume main_coro → return results
```

---

## Running the Examples

```bash
# Simple viewer
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --plan_name simple_plan

# With deadline
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --deadline_ms 100 --plan_name complex_plan

# With per-node timeout
echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --node_timeout_ms 50 --plan_name complex_plan

# Benchmark to see timing
engine/bin/rankd --async_scheduler --bench 100 --plan_name my_plan
```
