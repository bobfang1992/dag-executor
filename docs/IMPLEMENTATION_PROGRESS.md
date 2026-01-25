# Implementation Progress

This document tracks the implementation status of all features in the dag-executor project.

## ✅ Completed Steps

### Step 00: Minimal Engine Skeleton
- `engine/src/main.cpp` - rankd binary reading JSON from stdin, writing to stdout
- `engine/CMakeLists.txt` - C++23 build with nlohmann/json
- `scripts/ci.sh` - Build + smoke test gate
- Returns 5 synthetic candidates (ids 1-5)
- Handles request_id (echo or generate) + engine_request_id

### Step 01: Plan Loading + DAG Execution
- `--plan <path>` CLI argument for rankd (using CLI11)
- `engine/include/plan.h` + `engine/src/plan.cpp` - Plan/Node structs, JSON parsing
- `engine/include/executor.h` + `engine/src/executor.cpp` - Validation + topo sort execution
- `engine/include/task_registry.h` + `engine/src/task_registry.cpp` - Task registry with `viewer.follow` and `take`
- Fail-closed validation: schema_version, unique node_ids, valid inputs, known ops, cycle detection
- Test artifacts: `demo.plan.json`, `cycle.plan.json`, `missing_input.plan.json`

### Step 02: Columnar RowSet Model
- `engine/include/column_batch.h` - ColumnBatch with id column + validity + DebugCounters
- `engine/include/rowset.h` - RowSet with batch/selection/order + materializeIndexViewForOutput()
- `take` shares batch pointer (no column copy), materialize_count stays 0
- Iteration semantics: order > selection > [0..N), with order+selection filtering
- `engine/tests/test_rowset.cpp` - Unit tests for RowSet iteration and take behavior

### Step 03: Registries + Codegen
- `registry/keys.toml` - Key Registry with 8 keys
- `registry/params.toml` - Param Registry with 3 params
- `registry/features.toml` - Feature Registry with 2 features
- `dsl/src/codegen.ts` - Generates TS tokens, C++ headers, and JSON artifacts with SHA-256 digests
- `--print-registry` flag for rankd to output registry digests
- CI runs `pnpm -C dsl run gen:check` to verify generated outputs are up-to-date

### Step 04: TaskSpec Validation
- `engine/include/task_registry.h` - TaskSpec, ParamField, ValidatedParams types
- `engine/include/sha256.h` - Header-only SHA256 for digest computation
- TaskSpec as single source of truth for task validation
- Strict fail-closed validation: missing required params, wrong types, unexpected fields
- `task_manifest_digest` computed via SHA256 of canonical TaskSpec JSON

### Step 05a: ParamTable and param_overrides Validation
- `engine/include/param_table.h` - ParamTable class with typed getters, validation helpers, ExecCtx
- Request-level `param_overrides` validation against registry metadata
- Fail-closed semantics: unknown params, non-writable, deprecated/blocked, wrong types, non-finite floats
- Catch2 testing framework integration

### Step 05b: vm Task and Expression Evaluation
- `engine/include/plan.h` - ExprNode struct for recursive expression trees
- `engine/include/expr_eval.h` - Expression evaluation with null propagation
- `vm` task: evaluates expressions per row, writes float columns
- ExprNode ops: const_number, const_null, key_ref, param_ref, add, sub, mul, neg, coalesce

### Step 06: filter Task and Predicate Evaluation
- `engine/include/pred_eval.h` - Predicate evaluation with null semantics
- `filter` task: evaluates predicates, updates selection without copying columns
- PredNode ops: const_bool, and, or, not, cmp, in, is_null, not_null

### Step 07: StringDictColumn, concat task, and output contracts
- `engine/include/column_batch.h` - StringDictColumn with dictionary-encoded strings
- `concat` task: concatenates two RowSets with schema validation

### Step 08: Regex PredIR with Dictionary Optimization
- RE2 dependency for regex evaluation
- Dict-scan optimization: regex runs once per dict entry (O(dict_size)), lookup via codes (O(1))

### Step 09: TypeScript DSL Runtime
- `dsl/packages/runtime/` - TypeScript runtime package (plan.ts, expr.ts, pred.ts)
- `dsl/packages/generated/` - Generated Key/Param/Feature tokens

### Step 10: QuickJS-based Plan Compiler
- `dsl/packages/compiler/` - QuickJS-based dslc compiler
- Sandbox disables: eval, Function, process, require, module, dynamic imports
- Validates artifacts: no undefined, no functions, no symbols, no cycles

### Step 10.5: Central Plan Store
- Plan store model: `plans/` → `artifacts/plans/`, `examples/plans/` → `artifacts/plans-examples/`
- `manifest.json` (committed SSOT) and generated `index.json`
- Engine plan loading by name: `--plan_name`, `--plan_dir`, `--list-plans`

### Step 11.1-11.4: Capabilities (RFC 0001)
- DSL: `ctx.requireCapability(capId, payload?)`, node-level extensions
- Engine: capability registry, payload validation, digest computation
- Codegen: `registry/capabilities.toml` → TS + C++ generated code

### Step 12.1-12.4: writes_effect (RFC 0005)
- Writes Contract: `UNION(writes, writes_effect)` for declaring task writes
- ADT types: `EffectKeys`, `EffectFromParam`, `EffectSwitchEnum`, `EffectUnion`
- Evaluator: `eval_writes()` returns `Exact(K) | May(K) | Unknown`
- Runtime schema drift audit with `--dump-run-trace`

### Step 13.1: AST Extraction for vm() Natural Expressions
- `dsl/packages/compiler/src/expr-compiler.ts` - Compiles TS expression AST to ExprIR
- Task-based extraction using `TASK_EXTRACTION_INFO`
- Supports: `Key.x * P.y`, `coalesce(a, b)`, arithmetic ops, negation

### Step 13.2: AST Extraction for filter() Natural Predicates
- `dsl/packages/compiler/src/pred-compiler.ts` - Compiles TS predicate AST to PredIR
- `regex` global function for natural predicate syntax
- Supports: `&&`, `||`, `!`, comparisons, null checks, `regex(Key.x, "pat")`

### Sort Task Implementation
- `engine/src/tasks/sort.cpp` - Sort task using permutation vector
- Updates `PermutationVector` only (no column materialization)

### Visualizer Step 01: Live Plan Editor
- `tools/visualizer/` - React + Monaco + Three.js visualization tool
- Live TypeScript editing with Monaco editor and DSL intellisense

### Step 14.1: RequestContext with user_id
- `engine/include/request.h` - RequestContext, ParseResult, parse_user_id
- Required `user_id` field in rank requests (positive uint32)
- Fail-closed validation

### Step 14.2: Endpoint Registry + EndpointRef
- `registry/endpoints.{dev,test,prod}.toml` - Per-env endpoint definitions
- `dsl/packages/generated/endpoints.ts` - Generated branded EndpointId type and EP object
- `artifacts/endpoints.{dev,test,prod}.json` - Per-env JSON with two digests
- `engine/include/endpoint_registry.h` - C++ EndpointRegistry with LoadFromJson
- `engine/include/task_registry.h` - Added EndpointRef to TaskParamType
- `engine/src/main.cpp` - `--env` and `--artifacts_dir` flags
- Two-digest system: `endpoint_registry_digest` (env-invariant) + `endpoints_config_digest` (env-specific)
- Cross-env validation: same endpoint_ids with same name/kind across envs
- Fail-closed: only `static` resolver supported

### Step 14.4: Redis-backed Tasks
- `engine/include/redis_client.h` - RedisClient wrapper using hiredis
- `engine/include/io_clients.h` - Per-request IoClients cache for Redis connections
- `engine/src/tasks/viewer.cpp` - Source task: fetches viewer's user data from Redis
- `engine/src/tasks/follow.cpp` - Transform task: fans out to followees with country hydration
- `engine/src/tasks/recommendation.cpp` - Transform task: fetches cached recommendations
- `engine/src/tasks/media.cpp` - Transform task: fans out to media items
- Task model change: `viewer` is the only source task; `follow`/`recommendation`/`media` are transforms
- EP global: endpoint references available as `EP.redis.redis_default` (no import needed)
- CI: Redis service container (Linux) / Homebrew (macOS), `scripts/seed_redis.sh` for test data

### Step 14.4 Hardening
- Runtime validation: `assertStringOrNull` for trace, `assertEndpointId` for endpoint params
- Consistent Redis error handling: all tasks fail-fast on Redis errors (connection/command errors throw)
- Missing data (empty Redis result) handled gracefully as null columns

### Step 14.5a: Inflight Limiting and Benchmark Mode
- `engine/include/inflight_limiter.h` - Per-endpoint semaphore for inflight request limiting
- `engine/include/thread_pool.h` - ThreadPool class with IO pool singleton
- `--bench <iterations>` flag for benchmark mode with timing statistics
- `--bench_concurrency` flag for concurrent request load testing
- Inflight limiting respects `policy.max_inflight` from endpoint config

### Step 14.5b: Within-request DAG Parallel Scheduler
- `engine/include/cpu_pool.h` - CPU thread pool for compute tasks (default 8 threads)
- `engine/src/dag_scheduler.cpp` - Parallel DAG scheduler with Kahn's algorithm
- Two-pool architecture: IO tasks → IO pool, compute tasks → CPU pool
- `is_io` field in TaskSpec to mark blocking IO tasks
- Thread-safe RedisClient, IoClients, ExecStats (mutex/atomic protection)
- `--cpu_threads`, `--within_request_parallelism` CLI flags
- `sleep` task for latency injection and scheduler testing
- Deterministic schema_deltas (sorted by topo order)
- Fail-fast error propagation
- See [docs/THREADING_MODEL.md](THREADING_MODEL.md) for architecture details

### Step 14.5c.1: libuv Event Loop + Sleep Awaitable (Coroutine MVP)
- **Goal**: Prove coroutine suspend/resume on single libuv loop thread
- **Architecture**:
  ```
  ┌─────────────────────────────────────────────────────────────┐
  │                      DAG Scheduler                          │
  │  ┌─────────────────────┐     ┌─────────────────────────────┐│
  │  │   CPU Thread Pool   │     │       EventLoop (1 thread)  ││
  │  │   (vm, filter, sort)│     │   libuv: poll IO            ││
  │  └─────────────────────┘     │   100+ Redis in flight      ││
  │            │ Post()          │   On complete: resume coro  ││
  │            ▼                 └─────────────────────────────┘│
  │  ┌─────────────────────┐                                    │
  │  │ Suspended Coroutines│◄── hiredis async (future step)    │
  │  └─────────────────────┘                                    │
  └─────────────────────────────────────────────────────────────┘
  ```
- **Files**:
  - `engine/include/event_loop.h` - EventLoop class with thread-safe `Post()`
  - `engine/src/event_loop.cpp` - libuv loop lifecycle, queue drain, edge case handling
  - `engine/include/coro_task.h` - `Task<T>` lazy coroutine with exception propagation
  - `engine/include/uv_sleep.h` - `SleepMs` awaitable using `uv_timer_t`
  - `docs/event_loop_architecture.md` - Architecture diagrams
- **Thread safety** (Codex-hardened):
  - `Post()` rejects after `Stop()` begins
  - Stop/Destroy from loop thread supported (detaches, leaks loop safely)
  - Shared exit state via `shared_ptr` for safe cleanup
- **Validation**: Two `Sleep(50ms)` complete in ~52ms (proves parallel suspension)
- **Tests**: 26 test cases, 38 assertions
- See PR #58, Issue #59

### Step 14.5c.2: Async Redis Awaitables (libuv + hiredis)
- **Goal**: Build async Redis IO bridge - convert hiredis callbacks to `co_await`-friendly awaitables
- **Files**:
  - `engine/include/async_inflight_limiter.h` - Coroutine-friendly FIFO queue for concurrency control
  - `engine/include/async_redis_client.h` - AsyncRedisClient with `HGet`, `LRange`, `HGetAll` awaitables
  - `engine/src/async_redis_client.cpp` - hiredis async + libuv integration
  - `engine/include/async_io_clients.h` - Per-endpoint client cache
  - `engine/src/async_io_clients.cpp` - Client creation on loop thread
  - `engine/tests/test_async_redis.cpp` - Unit + integration tests
- **Key Design**:
  - All async ops on loop thread (no locks needed)
  - `ParsedReply` extracts data in callback before hiredis frees reply (avoids UAF)
  - RAII `Guard` releases permit on awaitable completion
  - Request timeouts via `uv_timer_t` (prevents stalled connections from blocking)
  - `CommandStateRef` with `weak_ptr` for safe late-reply handling after timeout
- **Safety hardening** (Codex-reviewed, 10+ P1 fixes):
  - `ctx_ptr_` double-pointer pattern for disconnect-while-waiting
  - Capture loop ref before `issue_command` to avoid UAF on error path
  - Permit released immediately on timeout (don't wait for reply that may never come)
  - Client/Task lifetime requirements documented
- **Tests**: 30 assertions in 10 test cases (unit + Redis integration)
- See PR #61

### Step 14.5c.3: Coroutine DAG Scheduler on libuv Loop
- **Goal**: Replace thread-pool DAG scheduler with coroutine-based scheduler on single libuv thread
- **Files**:
  - `engine/include/cpu_offload.h` - `OffloadCpu` awaitable for CPU work on thread pool
  - `engine/include/async_dag_scheduler.h` - `ExecCtxAsync` struct, async scheduler declarations
  - `engine/src/async_dag_scheduler.cpp` - Async DAG scheduler with Kahn's algorithm
  - `engine/include/task_registry.h` - Added `AsyncTaskFn` and `run_async` to TaskSpec
  - `engine/src/tasks/{viewer,follow,media,recommendation,sleep}.cpp` - Added `run_async` methods
  - `engine/src/main.cpp` - `--async_scheduler` flag
- **Architecture**:
  ```
  ┌──────────────────────────────────────────────────────────────────┐
  │                  Async DAG Scheduler (single thread)              │
  │  ┌────────────────────────┐    ┌────────────────────────────────┐│
  │  │   CPU Thread Pool      │    │   EventLoop (loop thread)      ││
  │  │   (vm, filter, sort)   │◄───│   - Drives libuv poll          ││
  │  │   via OffloadCpu       │    │   - Resumes coroutines         ││
  │  └────────────────────────┘    │   - 100+ Redis in flight       ││
  │            │                   └────────────────────────────────┘│
  │            │ Post()                       ▲                       │
  │            ▼                              │                       │
  │  ┌────────────────────────────────────────┴─────────────────────┐│
  │  │ Suspended Coroutines (Task<RowSet>)                          ││
  │  │   - IO tasks: co_await AsyncRedisClient                       ││
  │  │   - CPU tasks: co_await OffloadCpu(fn) → resume on loop      ││
  │  └──────────────────────────────────────────────────────────────┘│
  └──────────────────────────────────────────────────────────────────┘
  ```
- **Key Design**:
  - `run_async` is OPTIONAL in TaskSpec; defaults to wrapping sync `run()` with `OffloadCpu`
  - All scheduler state on loop thread (no mutexes in AsyncSchedulerState)
  - `ExecCtxAsync` struct carries EventLoop*, AsyncIoClients* for async tasks
  - `AsyncIoClients` is process-level (shared across requests)
  - Independent DAG branches run concurrently via coroutine suspension
- **OffloadCpu pattern**:
  ```cpp
  auto result = co_await OffloadCpu(loop, [&]() {
    // CPU-intensive work on thread pool
    return compute();
  });
  // Resume on loop thread with result
  ```
- **Benefits**:
  - 1 thread handles 100+ concurrent Redis calls (vs 8 threads = 8 calls)
  - Natural backpressure (coroutines suspend, don't spawn threads)
  - Fine-grained yielding: node can yield mid-execution on IO
- **Validation**: All existing tests pass (290 assertions in 53 test cases)
- **Usage**: `echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler`

### Step 14.5c.5b: Request Deadline + Node Timeout ✅
- **Goal**: Add deadline/timeout support for async scheduler with graceful timeout handling
- **Status**: Complete (PR #63 merged)
- **Files**:
  - `engine/include/deadline.h` - Deadline types and helpers
  - `engine/include/cpu_offload.h` - `OffloadCpuWithTimeout` awaitable for CPU tasks
  - `engine/include/thread_pool.h` - Added `wait_idle()` for drain semantics
  - `engine/src/async_dag_scheduler.cpp` - Deadline checks, shared_ptr for safe capture
  - `engine/src/main.cpp` - `--deadline_ms` and `--node_timeout_ms` CLI flags
  - `engine/src/tasks/fixed_source.cpp` - Pure source task for CI-safe testing (no Redis)
  - `engine/src/tasks/busy_cpu.cpp` - CPU spin task for timeout testing
- **Key Design**:
  - **First-wins pattern**: Timer vs CPU completion race, all state mutations on loop thread
  - **Timeout, not cancellation**: CPU work runs to completion, result discarded on timeout
  - **Safe capture**: ExecCtx data copied to shared_ptr so CPU lambda owns all data
  - **Drain semantics**: `ThreadPool::wait_idle()` ensures CPU jobs complete before EventLoop destruction
  - **Deadline checks**: Before spawning new nodes (`spawn_ready_nodes`) and at node start
  - **Effective deadline**: `min(request_deadline, now + node_timeout)` computed per node
- **CLI flags**:
  - `--deadline_ms N`: Request-level deadline (absolute time from request start)
  - `--node_timeout_ms N`: Per-node timeout (max execution time per node)
- **Tests**: 15 tests (10 deadline + 5 timeout)
  - Expired/short/generous deadlines
  - Multi-stage pipelines with timeout
  - Repeated operations (leak check)
  - Alternating success/timeout patterns
- **Usage**: `echo '{"user_id": 1}' | engine/bin/rankd --async_scheduler --deadline_ms 100`

### Step 14.5c.5c: Async Task Timeout ✅
- **Goal**: Enforce timeout for async tasks (Redis, `sleep`) not just CPU tasks
- **Status**: Complete (PR #64)
- **Files**:
  - `engine/include/cpu_offload.h` - `AsyncWithTimeout` awaitable (rewritten)
  - `engine/src/async_dag_scheduler.cpp` - Wire `AsyncWithTimeout` for async tasks
  - `engine/tests/test_dag_scheduler.cpp` - 8 new tests for async timeout
- **Key Design**:
  - **First-wins shared-state pattern**: Timer vs async task completion race
  - **Runner coroutine**: Detached coroutine awaits inner task, posts completion to loop
  - **State holds runner**: `State::runner` keeps coroutine frame alive until completion
  - **RunnerCleanup RAII**: Breaks reference cycle by posting cleanup after `final_suspend`
  - **All resumes via Post()**: Ensures reentrancy safety, no resume inside await_suspend
  - **await_suspend_returned flag**: Enables safe Post() fallback during shutdown
  - **Copy ALL ctx data**: Wrapper coroutine captures shared_ptrs to params, expr_table, pred_table, request, endpoints, resolved_refs - prevents UAF on timeout
- **Late completion handling**: When timeout wins, async task continues in runner until completion, result discarded (no cancellation). `LateCompletionCounter` test hook verifies this.
- **Tests**: 8 new tests
  - `sleep respects request deadline` / `sleep respects node timeout`
  - `late completion increments counter`
  - `mixed async+CPU pipeline timeout` (async phase / CPU phase)
  - `mixed async+CPU pipeline succeeds`
  - `parallel async tasks both respect deadline`
- **Validation**: 28 test cases, 138 assertions pass; 75 CI tests pass

---

## 🔲 Not Yet Implemented

### Step 14.5c.future: EventLoop Benchmarking
- [ ] Benchmark EventLoop throughput (posts/sec, timers/sec)
- [ ] Compare coroutine overhead vs thread pool for IO-bound workloads
- [ ] Profile memory usage (coroutine frames vs thread stacks)
- [ ] Measure latency distribution under load

### Step 14.2 Follow-up: Endpoint Registry Hardening
- [ ] Validate endpoint digests: recompute from parsed entries, compare against JSON values, reject mismatched --env
- [ ] Enforce integer checks in TOML parser for ports/timeouts (floats currently accepted but C++ requires int)
- [ ] Propagate endpoint_kind constraint to TS types (narrow EndpointId to RedisEndpointId/HttpEndpointId)
- [ ] Add identifier sanitization for endpoint names (guard against digits at start, reserved words, collisions)

### Registries
- [ ] Lifecycle/deprecation enforcement

### DSL Layer
- [ ] Fragment authoring surface

### Engine Core
- [ ] Budget enforcement

### Tasks
- [ ] fetch_features / call_models
- [ ] dedupe
- [ ] join (left/inner/semi/anti)
- [ ] extract_features

### HTTP Server
- [ ] POST /rank endpoint (currently stdin/stdout only)

### Audit Logging
- [ ] Request-level logs
- [ ] Task-level logs
- [ ] Param-level logs
- [ ] Critical path tracing

### Tooling
- [ ] Plan skeleton generator
- [ ] Task skeleton generator
- [ ] SourceRef generation
