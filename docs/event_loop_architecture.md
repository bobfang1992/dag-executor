# EventLoop Architecture

## Current Architecture (Thread Pools)

```
┌─────────────────────────────────────────────────────────────────┐
│                      DAG Scheduler                               │
│  ┌─────────────────────────────┐  ┌─────────────────────────────┐│
│  │       CPU Thread Pool       │  │       IO Thread Pool        ││
│  │        (4 threads)          │  │        (8 threads)          ││
│  ├─────────────────────────────┤  ├─────────────────────────────┤│
│  │ [T1] vm() executing         │  │ [T1] BLOCKED on Redis GET   ││
│  │ [T2] filter() executing     │  │ [T2] BLOCKED on Redis GET   ││
│  │ [T3] idle                   │  │ [T3] BLOCKED on Redis LRANGE││
│  │ [T4] sort() executing       │  │ [T4] BLOCKED on Redis GET   ││
│  │                             │  │ [T5] BLOCKED on Redis HGET  ││
│  │                             │  │ [T6] BLOCKED on Redis GET   ││
│  │                             │  │ [T7] idle                   ││
│  │                             │  │ [T8] idle                   ││
│  └─────────────────────────────┘  └─────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                                           │
                Problem: 8 threads can only do 8 concurrent Redis calls
                         Threads spend 95% of time BLOCKED waiting
```

## Target Architecture (Coroutines + EventLoop)

```
┌─────────────────────────────────────────────────────────────────┐
│                      DAG Scheduler                               │
│  ┌─────────────────────────────┐  ┌─────────────────────────────┐│
│  │       CPU Thread Pool       │  │       EventLoop             ││
│  │        (4 threads)          │  │      (1 thread)             ││
│  ├─────────────────────────────┤  ├─────────────────────────────┤│
│  │ [T1] vm() executing         │  │                             ││
│  │ [T2] filter() executing     │  │   libuv event loop          ││
│  │ [T3] idle                   │  │   ┌─────────────────────┐   ││
│  │ [T4] sort() executing       │  │   │ Poll for IO events  │   ││
│  │                             │  │   │                     │   ││
│  └─────────────────────────────┘  │   │ 100+ pending Redis  │   ││
│               │                   │   │ calls in flight     │   ││
│               │                   │   │                     │   ││
│               │ Post()            │   │ On completion:      │   ││
│               │ (thread-safe)     │   │ resume coroutine    │   ││
│               ▼                   │   └─────────────────────┘   ││
│  ┌─────────────────────────────┐  │                             ││
│  │  Suspended Coroutines       │  │  hiredis async context      ││
│  │  ┌────┐┌────┐┌────┐┌────┐   │◄─┤  (non-blocking Redis)       ││
│  │  │coro││coro││coro││coro│...│  │                             ││
│  │  └────┘└────┘└────┘└────┘   │  └─────────────────────────────┘│
│  │  (100+ waiting on Redis)    │                                 │
│  └─────────────────────────────┘                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Interaction Flow

```
Main Thread              CPU Pool              EventLoop Thread
    │                        │                        │
    │  execute DAG           │                        │
    │───────────────────────►│                        │
    │                        │                        │
    │                   ┌────┴────┐                   │
    │                   │ viewer  │                   │
    │                   │ task    │                   │
    │                   └────┬────┘                   │
    │                        │                        │
    │                        │ Post(start coroutine)  │
    │                        │───────────────────────►│
    │                        │                        │
    │                        │            ┌───────────┴───────────┐
    │                        │            │ coro starts           │
    │                        │            │ co_await redis.get()  │
    │                        │            │ coro SUSPENDS         │
    │                        │            │                       │
    │                        │            │ hiredis async GET     │
    │                        │            │ (non-blocking)        │
    │                        │            └───────────┬───────────┘
    │                        │                        │
    │                        │            ┌───────────┴───────────┐
    │                        │            │ ... loop handles      │
    │                        │            │ other IO events ...   │
    │                        │            └───────────┬───────────┘
    │                        │                        │
    │                        │            ┌───────────┴───────────┐
    │                        │            │ Redis reply arrives   │
    │                        │            │ coro RESUMES          │
    │                        │            │ co_return result      │
    │                        │            └───────────┬───────────┘
    │                        │                        │
    │                        │◄───────────────────────│
    │                        │  on_node_complete()    │
    │                        │                        │
```

## Why Post() Must Be Thread-Safe

```
┌──────────────┐         ┌──────────────┐
│  CPU Pool    │         │  EventLoop   │
│  Thread      │         │  Thread      │
└──────┬───────┘         └──────┬───────┘
       │                        │
       │  Post(start_coro)      │
       │───────────────────────►│  CPU thread posts work
       │                        │  to EventLoop
       │                        │
       │                        │  (later, on IO complete)
       │  on_node_complete()    │
       │◄───────────────────────│  EventLoop posts back
       │                        │  to scheduler
       │                        │

Both directions need thread-safe posting!
```

## Design Constraints

Based on the architecture above, our EventLoop needs:

| Feature | Required? | Supported | Notes |
|---------|-----------|-----------|-------|
| Dedicated loop thread | Yes | Yes | Can't block scheduler threads |
| Thread-safe Post() | Yes | Yes | Bidirectional communication |
| Stop from any thread | Yes | Yes | Scheduler controls lifecycle |
| Stop from loop thread | No | Yes | Drains accepted callbacks, detaches thread |
| Destroy from callback | No | Yes | Leaks loop (safe), detaches thread |

## Edge Case Handling

Stop/Destroy from loop thread are supported but have caveats:
- **Stop from loop thread**: Drains queue, detaches thread to avoid deadlock
- **Destroy from callback**: Leaks uv_loop (can't close while inside uv_run), but no crash/UAF
- Uses shared_ptr for exit state so detached thread doesn't access freed memory
