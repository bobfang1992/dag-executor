# Step 14.5c.2 — Async Redis Awaitables (libuv + hiredis async) with Extensible IO Shape

This step builds the **async IO bridge**: convert hiredis async callbacks into `co_await`-friendly awaitables running on the **single libuv loop thread** from Step 14.5c.1.

**Important:** This is still an incremental step.
- We are **not** rewriting the DAG scheduler yet (that is Step 14.5c.3).
- We are **not** removing thread pools yet.
- We are building the IO adapter in a way that will make **Postgres/SQLite/Kafka** integration straightforward later.

---

## Goal
1. Implement `AsyncRedisClient` attached to the shared `EventLoop` (libuv).
2. Provide coroutine APIs that tasks can call:
   - `co_await redis.HGet(key, field)`
   - `co_await redis.LRange(key, start, stop)`
   - (optional) `co_await redis.HMGet(key, fields)`
3. Enforce **per-endpoint inflight limiting** using the existing `InflightLimiter` (or an async-friendly equivalent).
4. Provide **tests**:
   - Unit tests for awaitable behavior (no Redis)
   - Optional integration tests with local Redis harness (not required in default CI)

---

## Why this matters for future IO kinds (design constraint)
We want a single, consistent shape for IO across kinds:

### A) “True async” IO (event-loop driven)
- Redis via hiredis async + libuv adapter (this step)
- Postgres can later use a nonblocking socket + `uv_poll_t` integration
- Kafka can later use a poll-loop thread that posts completions back to the loop

### B) “Blocking offload” IO (thread-pool driven, still awaitable)
- SQLite is usually best as blocking offload to a small DB pool
- Postgres can start as blocking libpq offload, then migrate to true async later

**Design rule:** tasks should not care which route is used.
Tasks call `co_await io.xxx()` and do not manage callbacks, threads, or semaphores themselves.

---

## Scope (this PR only)
Deliverables:
1. `AsyncRedisClient` that:
   - connects to a resolved redis endpoint (`host:port`)
   - attaches `redisAsyncContext` to the libuv loop via hiredis `adapters/libuv.h`
   - exposes coroutine awaitables for `HGET` and `LRANGE` (and optionally `HMGET`)
2. `AsyncIoClients` (process-level, loop-affine) cache:
   - map `endpoint_id -> AsyncRedisClient`
   - created/lives on loop thread (no locks needed if all access is on loop)
3. Inflight limiting integration:
   - acquire before sending a Redis command
   - release on completion callback (success or error)
   - keyed by `endpoint_id`
4. Tests:
   - Unit: coroutine suspension/resume + error propagation without real Redis
   - Integration (optional, label-gated): local Redis harness seeded data, run 100 concurrent LRANGE awaits

Non-goals:
- No DAG scheduler rewrite.
- No conversion of all tasks to coroutines yet.
- No Redis pipelining/batching optimizations.
- No reconnection/backoff policy beyond simple fail-fast.

---

## Files to create/modify (suggested)
### New
- `engine/include/async_redis_client.h`
- `engine/src/async_redis_client.cpp`
- `engine/include/async_io_clients.h`
- `engine/src/async_io_clients.cpp` (optional; can be header-only if small)
- `engine/include/redis_awaitables.h` (optional; if you want awaitable types separated)

### Modified
- `engine/CMakeLists.txt` (link libuv + hiredis async adapter include path)
- `CLAUDE.md` or `docs/` (short note: async redis is available; future IO kinds follow same pattern)

### Tests
- `engine/tests/test_async_redis_awaitable.cpp` (unit-only, no redis)
- `engine/tests/test_async_redis_integration.cpp` (optional, Redis-required label)

---

## 1) Async Redis client design

### 1.1 Endpoint resolution (reuse existing)
- Resolve `endpoint_id -> EndpointSpec` via `EndpointRegistry`
- Verify `kind == redis`
- Use `host:port` for connection

### 1.2 Attach hiredis async to libuv
Use hiredis async + libuv adapter:
- create `redisAsyncContext* c = redisAsyncConnect(host, port)`
- attach to uv loop with `redisLibuvAttach(c, uv_loop_t*)` (from `adapters/libuv.h`)
- register connect/disconnect callbacks to capture errors

Fail-fast behavior:
- If connect fails, throw (or surface error) to the awaiting coroutine.
- For MVP, no auto-reconnect; callers see an error.

### 1.3 Coroutine awaitable shape
Create an awaitable object per command that:
- stores:
  - coroutine handle
  - result storage (parsed response or exception)
  - an inflight permit token (RAII)
- `await_suspend(h)`:
  - acquires inflight permit (may block/yield depending on limiter)
  - issues `redisAsyncCommandArgv(...)` (prefer Argv to avoid quoting issues)
  - registers a static callback that captures `this`
- callback:
  - parses `redisReply*` into the expected C++ type
  - stores result / exception
  - posts `handle.resume()` via `EventLoop::Post(...)`

Important:
- Always resume via `Post(...)` (even if callback runs on loop thread) to keep one consistent resume path.
- Ensure resources are cleaned up exactly once (permit released, awaitable destroyed).

### 1.4 Result parsing (MVP)
For MVP:
- `HGET` returns `std::optional<std::string>` (nil => nullopt)
- `LRANGE` returns `std::vector<std::string>` (empty => empty vector)
- treat non-string element types as error (throw)

---

## 2) Inflight limiting (per endpoint)
Reuse the existing `InflightLimiter` policy:
- capacity = endpoint `policy.max_inflight` else default

Where to apply:
- **inside AsyncRedisClient / awaitable**, not in tasks
- acquire before sending command
- release in callback (or in awaitable destructor on error path)

Note:
- This is inflight/backpressure, not global QPS limiting.
- Upstream request rate limiting does not account for internal fanout amplification.

---

## 3) Async IO client caching (future-proof)
Create `AsyncIoClients` (process-level, loop-affine):
- stores a map `endpoint_id -> AsyncRedisClient`
- provides `GetRedis(endpoint_id)` returning a reference/pointer

Rules:
- Creation must happen on loop thread.
- Access should happen on loop thread as well (assert or enforce via `EventLoop::Post`).
- Future IO kinds (postgres/sqlite/kafka) can add additional maps:
  - `postgres_by_endpoint`
  - `sqlite_by_endpoint`
  - `kafka_by_endpoint`

This avoids committing to a “Store abstraction” too early while still making future IO expansion clean.

---

## 4) Tests

### 4.1 Unit tests (no Redis required)
Test awaitable mechanics using only the loop:
- Create a fake awaitable that resumes later via `EventLoop::Post` (you already did for Sleep).
- Validate:
  - coroutine suspends and resumes
  - exceptions propagate
  - Stop/shutdown does not UAF

These tests ensure the coroutine plumbing is correct independently of Redis.

### 4.2 Integration tests (optional, local-only or label-gated)
Using Step 14.3 local Redis harness + seeded data:
- Start redis + seed
- Run 100 concurrent `LRANGE follow:1 0 4` awaits and verify:
  - results match expected
  - completes without deadlock
- Add one failure case:
  - connect to invalid port and confirm awaitable returns error quickly (fail-fast)

Mark these tests with a label (e.g. `redis`) so default CI does not require Redis unless you already run Redis in CI.

---

## 5) Local verification commands
1. Start and seed Redis:
```bash
bash scripts/ci_redis_local.sh
```

2. Run integration test (if enabled):
```bash
ctest -L redis
```

---

## Acceptance
- `./scripts/ci.sh` passes (unit tests only; no Redis required).
- Integration test passes locally with seeded Redis (if implemented).
- Demonstrates that 100+ concurrent async Redis commands can be in-flight without occupying 100 threads.

---

## PR conventions
- PR title: **`Step 14.5c.2: async Redis awaitables (libuv + hiredis)`**
- Keep the PR focused on IO awaitables; do not rewrite the DAG scheduler yet.
