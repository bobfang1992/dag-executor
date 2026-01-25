# EventLoop Shutdown & Drain Contract

This document defines the hard contract for EventLoop lifecycle management, shutdown ordering, and interaction with thread pools.

## Lifecycle States

```
┌──────┐     Start()     ┌──────────┐     (thread ready)     ┌─────────┐
│ Idle │ ───────────────►│ Starting │ ─────────────────────► │ Running │
└──────┘                 └──────────┘                        └────┬────┘
                               │                                  │
                               │ Stop() during init               │ Stop()
                               ▼                                  ▼
                         ┌──────────┐                        ┌──────────┐
                         │ Stopping │◄───────────────────────│ Stopping │
                         └────┬─────┘                        └────┬─────┘
                              │                                   │
                              │ (cleanup complete)                │
                              ▼                                   ▼
                         ┌─────────┐                         ┌─────────┐
                         │ Stopped │                         │ Stopped │
                         └─────────┘                         └─────────┘
```

| State | Description |
|-------|-------------|
| **Idle** | Not started. `Post()` returns false. |
| **Starting** | `Start()` called, thread spawning. `Post()` returns false. |
| **Running** | Loop thread active. `Post()` accepts work. |
| **Stopping** | Shutdown in progress. `Post()` returns false. |
| **Stopped** | Loop thread exited. Destructor can run. |

## Required Invariants (Must-Not-Break Rules)

### Thread Ownership
All libuv operations happen on the loop thread. Never call `uv_*` functions from other threads.

### Post Contract

```cpp
bool Post(std::function<void()> fn);
```

| Condition | Behavior |
|-----------|----------|
| State == Running | Enqueue callback, return `true` |
| State != Running | Return `false`, do NOT enqueue, do NOT execute |

**Critical**: `Post()` must be safe to call from any thread at any time. After `Stop()` begins (state transitions to Stopping), `Post()` must return `false` and must never execute the callback.

### Stop Contract

```cpp
void Stop();
```

- **Idempotent**: Multiple calls do not deadlock or crash
- **Early state flip**: State transitions to `Stopping` atomically at the START of `Stop()`, before any other work
- **Drains accepted work**: Callbacks already enqueued when Stop begins will execute
- **Joins thread**: `Stop()` blocks until the loop thread exits (unless called from loop thread)

### Stop-from-Loop-Thread

If `Stop()` is called from within a callback (on the loop thread):
- Executes shutdown inline
- Detaches the thread to avoid self-join deadlock
- Destructor will wait for thread exit via `exit_state_` condition variable

**Warning**: Callbacks must not destroy the EventLoop. Destructor asserts if called from loop thread.

## Drain Contract (Thread Pool Integration)

### The Problem

`OffloadCpuWithTimeout` and `OffloadCpu` submit work to `GetCPUThreadPool()`. When work completes, they call `loop.Post()` to resume the coroutine. If the EventLoop is destroyed while CPU work is in-flight, the completion callback will call `Post()` on a destroyed object.

### The Solution

**Before destroying EventLoop, drain all thread pools that can Post back:**

```cpp
// Shutdown sequence
GetCPUThreadPool().wait_idle();  // Wait for all CPU work to complete
loop.Stop();                      // Now safe to stop
// EventLoop destructor runs
```

`ThreadPool::wait_idle()` blocks until `in_flight_ == 0`, ensuring no pending CPU jobs can call `Post()` after the loop is destroyed.

### Late Completion (Async Timeout)

When `AsyncWithTimeout` or `OffloadCpuWithTimeout` fires a timeout:
1. The timeout wins (sets `done = true`)
2. The coroutine resumes with timeout error
3. The CPU/async work continues in the background
4. When it completes, it checks `done` flag and finds it's a late completion
5. Late completion calls `Post()` to clean up

If `Post()` returns false (loop stopping), the cleanup is skipped safely because:
- Shared state uses `shared_ptr` (prevents UAF)
- Late completion checks `await_suspend_returned` flag before direct resume fallback

## Recommended Shutdown Sequence

For the engine/server/benchmark harness, follow this order:

```
1. Stop accepting new requests
   └─► Set a "shutting down" flag at the application level

2. Cancel/timeout in-flight requests
   └─► Existing deadline/timeout mechanisms handle this

3. Drain CPU thread pool
   └─► GetCPUThreadPool().wait_idle()
   └─► Ensures all OffloadCpu completions have Posted back

4. Stop the EventLoop
   └─► loop.Stop()
   └─► State → Stopping (rejects new Posts)
   └─► Drains remaining callbacks
   └─► Closes handles, stops loop

5. Join loop thread / destroy EventLoop
   └─► Destructor waits for thread exit
   └─► uv_loop_close() runs

6. Destroy remaining resources
   └─► AsyncIoClients, registries, etc.
```

### Example (from main.cpp benchmark mode)

```cpp
// Simplified shutdown
try {
    auto result = execute_plan_async_blocking(...);
    // ... process result
} catch (...) {
    // Error handling
}

// Drain CPU pool before EventLoop destruction
rankd::GetCPUThreadPool().wait_idle();

// EventLoop destructor runs here (calls Stop if needed)
```

## Implementation Details

### State Machine (Atomic CAS)

All state transitions use `compare_exchange_strong` to eliminate race windows:

```cpp
// In Stop():
State expected = State::Running;
if (!state_.compare_exchange_strong(expected, State::Stopping)) {
    // State changed - retry or handle other cases
}
```

### Post() Double-Check Pattern

```cpp
bool Post(std::function<void()> fn) {
    if (state_.load() != State::Running) {
        return false;  // Fast path rejection
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (state_.load() != State::Running) {
            return false;  // Re-check under lock
        }
        queue_.push(std::move(fn));
    }
    uv_async_send(&async_);
    return true;
}
```

The double-check prevents a race where `Stop()` drains the queue between the first check and the enqueue.

## Testing

Existing tests in `engine/tests/test_event_loop.cpp` verify:

| Test | Invariant |
|------|-----------|
| "Post before Start returns false" | Post rejected in Idle state |
| "Post after Stop returns false" | Post rejected in Stopped state |
| "Post during Stop is rejected" | Post rejected during Stopping transition |
| "Multiple Stop calls are idempotent" | Stop is safe to call repeatedly |
| "Stop from within callback" | Stop-from-loop-thread works |
| "Destruction without Stop" | Destructor calls Stop if needed |

## Related Documentation

- [event_loop_architecture.md](event_loop_architecture.md) - Overall EventLoop design
- [THREADING_MODEL.md](THREADING_MODEL.md) - Thread pool architecture
- [async_dag_scheduler_architecture.md](async_dag_scheduler_architecture.md) - Async scheduler and timeout handling
