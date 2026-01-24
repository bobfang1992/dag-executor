# Task Execution Flow

This document explains how tasks flow through the async DAG scheduler, from JSON parsing to result output.

## Overview

```
JSON Plan → Parse → Validate → Build DAG → Execute Nodes → Collect Results
```

## Example 1: Single Node (Source Task)

### Plan JSON
```json
{
  "nodes": [
    {"node_id": "v", "op": "viewer", "inputs": [], "params": {"endpoint": "ep_0001"}}
  ],
  "outputs": ["v"]
}
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

### Plan JSON
```json
{
  "nodes": [
    {"node_id": "v", "op": "viewer", "inputs": [], "params": {"endpoint": "ep_0001"}},
    {"node_id": "f", "op": "filter", "inputs": ["v"], "params": {"pred": "..."}}
  ],
  "outputs": ["f"]
}
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

### Plan JSON
```json
{
  "nodes": [
    {"node_id": "v", "op": "viewer", "inputs": []},
    {"node_id": "follow", "op": "follow", "inputs": ["v"]},
    {"node_id": "recs", "op": "recommendation", "inputs": ["v"]}
  ],
  "outputs": ["follow", "recs"]
}
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

### Plan JSON
```json
{
  "nodes": [
    {"node_id": "v", "op": "viewer", "inputs": []},
    {"node_id": "f", "op": "follow", "inputs": ["v"]},
    {"node_id": "vm1", "op": "vm", "inputs": ["f"], "params": {"expr": "..."}},
    {"node_id": "filter", "op": "filter", "inputs": ["vm1"], "params": {"pred": "..."}},
    {"node_id": "sort", "op": "sort", "inputs": ["filter"]},
    {"node_id": "take", "op": "take", "inputs": ["sort"], "params": {"limit": 10}}
  ],
  "outputs": ["take"]
}
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

### Plan JSON
```json
{
  "nodes": [
    {"node_id": "v", "op": "viewer", "inputs": []},
    {"node_id": "left", "op": "follow", "inputs": ["v"]},
    {"node_id": "right", "op": "recommendation", "inputs": ["v"]},
    {"node_id": "join", "op": "concat", "inputs": ["left"], "params": {"rhs": "right"}}
  ],
  "outputs": ["join"]
}
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

### Plan JSON
```json
{
  "nodes": [
    {"node_id": "v", "op": "viewer", "inputs": []},
    {"node_id": "follow", "op": "follow", "inputs": ["v"]},
    {"node_id": "recs", "op": "recommendation", "inputs": ["v"]},
    {"node_id": "media_f", "op": "media", "inputs": ["follow"]},
    {"node_id": "media_r", "op": "media", "inputs": ["recs"]},
    {"node_id": "vm_f", "op": "vm", "inputs": ["media_f"]},
    {"node_id": "vm_r", "op": "vm", "inputs": ["media_r"]},
    {"node_id": "merge", "op": "concat", "inputs": ["vm_f"], "params": {"rhs": "vm_r"}},
    {"node_id": "sort", "op": "sort", "inputs": ["merge"]},
    {"node_id": "take", "op": "take", "inputs": ["sort"], "params": {"limit": 50}}
  ],
  "outputs": ["take"]
}
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

## Task Type Summary

| Task | Has run_async | Runs On | Suspends For |
|------|---------------|---------|--------------|
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
