# Testing Guide

This document categorizes all tests in the dag-executor project by type and feature.

## Test Categories

| Category | Framework | Location | Run Command |
|----------|-----------|----------|-------------|
| C++ Unit | Catch2 | `engine/tests/` | `engine/bin/<test>` |
| TS Unit | Custom | `dsl/tools/test_*.ts` | `pnpm -C dsl run test:*` |
| Integration | Bash | `scripts/ci.sh` | `./scripts/ci.sh` |
| E2E | CI script | `scripts/ci.sh` | `./scripts/ci.sh` |

---

## C++ Unit Tests

### RowSet & Column Model (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_rowset.cpp` | RowSet iteration | Batch with sequential ids |
| | | take limits output and shares batch pointer |
| | | RowSet iteration with selection and order |
| | | take with selection and order combined |
| | | ActiveRows forEachIndex iterates correctly |
| | | RowSet truncateTo works correctly |

### Parameter Handling (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_param_table.cpp` | ParamTable | Basic set/get operations |
| | | Null value handling |
| | | fromParamOverrides with valid input |
| | | Nullable params |
| | | Rejects unknown param |
| | | Rejects wrong type |
| | | Rejects null for non-nullable |
| | | Rejects non-finite floats |
| | | Handles overflow |
| | | Empty/null input |

### Request Parsing (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_request.cpp` | user_id parsing | Accepts valid integers |
| | | Accepts valid string integers |
| | | Rejects invalid types |
| | | Rejects invalid values |
| | RequestContext | parse_request_context with valid input |
| | | parse_request_context with invalid input |

### Predicate Evaluation (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_pred_eval.cpp` | const_bool | const_bool predicate |
| | Logical ops | and, or, not predicates |
| | Null checks | is_null and not_null predicates |
| | Comparisons | cmp predicates with non-null values |
| | | cmp predicates with null operands (§7.2) |
| | | runtime null vs literal null distinction |
| | Membership | in predicate |
| | | key_ref in predicates |
| | Null semantics | null comparison semantics (per spec) |

### Regex (`engine/bin/regex_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_regex.cpp` | Regex predicates | regex with literal pattern |
| | | regex with param_ref pattern |
| | | regex with case-insensitive flag |
| | | regex on null row returns false |
| | | regex on missing column returns false |
| | | regex with missing param throws |
| | | invalid regex pattern throws |

### Sort Task (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_sort.cpp` | Sort task | orders floats ascending with nulls last |
| | | respects selection/order for strings and desc |
| | | handles string null-null comparisons safely |
| | | rejects invalid params or unsupported keys |

### Concat Task (`engine/bin/concat_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_concat.cpp` | concat task | produces correct output |
| | Integration | concat_plan.plan.json executes correctly |
| | Validation | concat_bad_arity.plan.json fails (missing rhs) |

### Endpoint Registry (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_endpoint_registry.cpp` | Loading | loads valid JSON |
| | Validation | rejects duplicate endpoint_id |
| | | rejects duplicate name |
| | | rejects invalid port |
| | | rejects unknown kind |
| | | rejects non-static resolver |
| | | rejects invalid endpoint_id format |
| | | rejects env mismatch |
| | | rejects digest mismatch |
| | Helpers | helper functions work |
| | Integration | loads real generated JSON |

### Writes Effect (`engine/bin/writes_effect_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_writes_effect.cpp` | EffectKeys | evaluates to Exact |
| | EffectFromParam | empty gamma returns Unknown |
| | | with gamma returns Exact |
| | | wrong type in gamma returns Unknown |
| | EffectSwitchEnum | matching case |
| | | different case |
| | | unknown param returns May(union) |
| | | missing case returns Unknown |
| | EffectUnion | combines Exact to Exact |
| | | with May results in May |
| | | with Unknown results in Unknown |
| | | empty returns Exact empty |
| | Serialization | serialize for all types |
| | Nesting | nested SwitchEnum in Union |

### Plan Info (`engine/bin/plan_info_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_plan_info_writes_eval.cpp` | writes_eval | vm + row-only ops fixture |
| | | fixed-writes source fixture |
| | | keys always sorted and unique |

### Schema Delta (`engine/bin/schema_delta_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_runtime_schema_delta.cpp` | Runtime audit | vm_and_row_ops fixture |
| | | fixed_source fixture (concat) |
| | | keys always sorted and unique |

### Inflight Limiter (`engine/bin/rankd_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_inflight_limiter.cpp` | Semaphore | basic acquire/release |
| | | blocks at limit |
| | | separate endpoints |
| | | guard move semantics |
| | | uses default if max_inflight <= 0 |
| | | reset clears all limiters |
| | | get_inflight_count tracks correctly |

### DAG Scheduler (`engine/bin/dag_scheduler_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_dag_scheduler.cpp` | Parallel exec | runs independent nodes concurrently |
| | Sequential | runs nodes serially |
| | Determinism | schema_deltas are deterministic |
| | Parity | parallel produces same results as sequential |
| | Sleep task | identity behavior |
| | Async scheduler | three-branch DAG with concurrent sleep + vm |
| | Fault injection | no deadlock or UAF on error |
| | Deadline | expired, short (1ms), generous |
| | Node timeout | short (1ms), generous |
| | Combined | both deadline and node_timeout |
| | Multi-stage | pipeline timeout, pipeline success |
| | fixed_source | no CPU offload path |
| | Stress | repeated timeout, repeated success, alternating |

### Event Loop (`engine/bin/event_loop_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_event_loop.cpp` | Basic | EventLoop basic post |
| | | EventLoop multiple posts |
| | Coroutines | Single SleepMs coroutine |
| | | Two concurrent SleepMs complete in parallel |
| | | Exception propagation in coroutine |
| | | Zero sleep completes immediately |
| | | Nested coroutine awaits |
| | Edge cases | Post before Start returns false |
| | | Post after Stop returns false |
| | | Stop from within callback |
| | | Multiple Stop calls are idempotent |
| | | Destruction without Stop |
| | | Destruction without Start |
| | | Post during Stop is rejected |
| | | Stop on loop thread drains callbacks |
| | Stress | many concurrent posts |
| | | posts from multiple threads |
| | | many concurrent sleeps |
| | | rapid start/stop cycles |

### Async Redis (`engine/bin/async_redis_tests`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_async_redis.cpp` | Inflight limiter | basic acquire/release |
| | | Guard RAII |
| | | coroutine acquire |
| | | FIFO ordering |
| | Connection | connection to invalid port |
| | | client creation only |
| | Commands | HGet |
| | | LRange |
| | | concurrent LRange with inflight limit |
| | Caching | AsyncIoClients caching |
| | Stress | stress test (optional) |

---

## TypeScript Unit Tests

### Writes Effect (`pnpm -C dsl run test:writes-effect`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_writes_effect.ts` | EffectKeys | empty, single, multiple, deduped |
| | EffectFromParam | empty gamma, with gamma, wrong type |
| | EffectSwitchEnum | matching, different, unknown, missing |
| | EffectUnion | exact+exact, with may, with unknown, empty |
| | Serialization | all effect types |
| | Parity | matches C++ behavior |

### Writes Effect Parity (`pnpm -C dsl run test:writes-effect-parity`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_writes_effect_parity.ts` | Cross-lang | TS eval == C++ eval for fixtures |

### AST Extraction (`pnpm -C dsl run test:ast`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `test_ast_extraction.ts` | Expression extraction | does NOT extract from unknown methods |
| | | extracts expr when outKey is variable |
| | | extracts expr with direct Key.* outKey |
| | | extracts from object form vm({ expr }) |
| | | rejects division operator |
| | | skips parenthesized builder-style |

### ESLint Plugin (`pnpm -C dsl/packages/eslint-plugin run test`)

| Test File | Feature | Test Cases |
|-----------|---------|------------|
| `rules/__tests__/run.ts` | no-dsl-import-alias | rejects Key import |
| | | rejects aliased Key |
| | | rejects coalesce import |
| | | allows non-restricted identifiers |
| | no-dsl-reassign | rejects const JK = Key |
| | | rejects let params = P |
| | | allows property access |
| | inline-expr-only | allows inline expression |
| | | rejects variable reference |
| | | rejects shorthand |
| | plan-restricted-imports | allows @ranking-dsl/runtime |
| | | allows fragment imports |
| | | rejects arbitrary imports |

---

## Integration Tests (CI)

### Batch 1: Basic Engine Tests

| Test # | Name | Feature |
|--------|------|---------|
| 1 | Step 00 fallback | Engine returns synthetic candidates |
| 2 | Demo plan | Plan loading and execution |
| 3 | Reject cycle.plan.json | Cycle detection |
| 4 | Reject missing_input.plan.json | Input validation |
| 5 | Print registry | Registry digest output |

### Batch 2: Param Validation

| Test # | Name | Feature |
|--------|------|---------|
| 6 | Reject bad_type_fanout | Type validation |
| 7 | Reject missing_fanout | Required param validation |
| 8 | Reject extra_param | Unknown param rejection |
| 9 | Reject bad_trace_type | Trace type validation |
| 10 | Accept null_trace | Nullable param handling |
| 11 | Reject large_fanout | Fanout limit enforcement |

### Batch 3: param_overrides

| Test # | Name | Feature |
|--------|------|---------|
| 12 | Valid param_overrides | Override acceptance |
| 13 | Reject unknown param | Unknown param rejection |
| 14 | Reject wrong type | Type validation |
| 15 | Reject null non-nullable | Null handling |
| 16 | Accept null nullable | Nullable override |

### Batch 4: Predicate Tests

| Test # | Name | Feature |
|--------|------|---------|
| 17 | Reject missing_pred_id | Pred table validation |
| 18 | Reject unknown_pred_id | Pred reference validation |
| 19 | Reject bad_pred_table_shape | Pred table structure |
| 20 | Reject bad_in_list | In-list type validation |
| 21 | String in-list | String membership |

### Batch 5: Concat and TS Plans

| Test # | Name | Feature |
|--------|------|---------|
| 22 | concat_plan | Concat task execution |
| 23 | Reject concat_bad_arity | Arity validation |
| 24 | reels_plan_a | E2E plan execution |
| 25 | reels_plan_a with overrides | Param override in plan |
| 26 | concat_plan | Concat with Redis |
| 27 | regex_plan | Regex filtering |

### Batch 6: Regex Tests

| Test # | Name | Feature |
|--------|------|---------|
| 28 | No RowSet internals access | Encapsulation |
| 29 | regex_demo | Regex literal pattern |
| 30 | regex_param_demo | Regex param pattern |
| 31 | Reject bad_regex_flags | Flag validation |

### Batch 7: Sandbox + Plan Store

| Test # | Name | Feature |
|--------|------|---------|
| 32 | Reject evil.plan.ts | QuickJS sandbox (eval) |
| 33 | Reject evil_proto.plan.ts | QuickJS sandbox (proto) |
| 34 | --plan_name | Plan store loading |
| 35 | Reject path traversal | Security |
| 36 | Reject slash in name | Name validation |
| 37 | index.json | Plan index generation |

### Batch 8: RFC0001 Capabilities

| Test # | Name | Feature |
|--------|------|---------|
| 38 | Reject name_mismatch | Capability naming |
| 39 | Reject bad_caps_unsorted | Sorted requirement |
| 40 | Reject bad_ext_key | Extension key validation |
| 41 | Reject bad_node_ext | Node extension validation |
| 42 | valid_capabilities | Valid capability usage |

### Batch 9: Compilation Tests

| Test # | Name | Feature |
|--------|------|---------|
| 43 | Compile valid_capabilities | dslc compiler |
| 44 | Compile multiple plans | Batch compilation |

### Batch 10: Engine RFC0001 Validation

| Test # | Name | Feature |
|--------|------|---------|
| 45 | Reject unknown capability | C++ validation |
| 46 | Reject unsorted capabilities | C++ validation |
| 47 | Reject extension not in capabilities | C++ validation |
| 48 | Reject node extension without capability | C++ validation |
| 49 | Reject non-empty payload for base | C++ validation |

### Batch 11: Digest Parity

| Test # | Name | Feature |
|--------|------|---------|
| 50 | Digest parity (with caps) | TS == C++ digest |
| 51 | Digest parity (no caps) | TS == C++ digest |
| 52 | Index has capabilities_digest | Index generation |
| 53 | Capability registry digest parity | Cross-lang parity |
| 54 | writes_effect evaluator parity | Cross-lang parity |

### Batch 12: writes_eval (RFC0005)

| Test # | Name | Feature |
|--------|------|---------|
| 55 | print-plan-info shows writes_eval | Plan info output |
| 56 | print-plan-info error on unsupported caps | Error handling |
| 57 | dump-run-trace shows schema_deltas | Runtime audit |

### Batch 13: AST Expression Extraction

| Test # | Name | Feature |
|--------|------|---------|
| 58 | Natural expression compilation | expr AST extraction |
| 59 | Division rejection | Unsupported op rejection |
| 60 | Mixed expression styles | Builder + natural mix |
| 61 | Reject invalid imports | Import restriction |
| 62 | ESLint catches aliased imports | Lint enforcement |
| 63 | Production plans pass ESLint | Plan quality |

### Batch 14: Task Manifest

| Test # | Name | Feature |
|--------|------|---------|
| 64 | tasks.toml in sync with C++ | SSOT validation |
| 65 | Task manifest digest parity | TS == C++ digest |
| 66 | tasks.ts exports task count | Codegen validation |

### Batch 15: Type Validation (Compile-time)

| Test # | Name | Feature |
|--------|------|---------|
| 67 | Reject invalid outKey type | TypeScript type check |
| 68 | Reject string fanout | TypeScript type check |
| 69 | Reject null count | TypeScript type check |
| 70 | Reject invalid expr type | TypeScript type check |
| 71 | Reject invalid pred type | TypeScript type check |
| 72 | Reject numeric trace | TypeScript type check |

### Batch 16: Endpoint Registry

| Test # | Name | Feature |
|--------|------|---------|
| 73 | Endpoint registry digest parity | TS == C++ digest |
| 74 | Endpoint env switching | --env flag |
| 75 | Endpoints.ts exports correct count | Codegen validation |

---

## E2E Test Plans

| Plan | Location | Tests |
|------|----------|-------|
| `reels_plan_a` | `plans/` | Full pipeline: viewer → follow → vm → filter → take |
| `concat_plan` | `plans/` | Source merging with concat task |
| `regex_plan` | `plans/` | Regex filtering on string columns |
| `parallel_sleep_plan` | `plans/` | Parallel scheduler (~25ms vs ~40ms sequential) |

---

## Running Tests

```bash
# All tests via CI script
./scripts/ci.sh

# C++ unit tests individually
engine/bin/rankd_tests           # 290 assertions - core engine tests
engine/bin/event_loop_tests      # 84 assertions - coroutine/libuv primitives
engine/bin/dag_scheduler_tests   # 97 assertions - sync + async scheduler + deadline/timeout
engine/bin/async_redis_tests     # ~20 assertions - async Redis (requires Redis)
engine/bin/concat_tests
engine/bin/regex_tests
engine/bin/writes_effect_tests
engine/bin/plan_info_tests
engine/bin/schema_delta_tests

# TypeScript tests
pnpm -C dsl run test:writes-effect
pnpm -C dsl run test:writes-effect-parity
pnpm -C dsl run test:ast
pnpm -C dsl/packages/eslint-plugin run test

# Specific Catch2 test case
engine/bin/rankd_tests "[param_table]"
engine/bin/rankd_tests "ParamTable basic*"

# Async scheduler tests (16 tests, 97 assertions)
engine/bin/dag_scheduler_tests "[async_scheduler]"
engine/bin/dag_scheduler_tests "*deadline*"
engine/bin/dag_scheduler_tests "*timeout*"
engine/bin/dag_scheduler_tests "*repeated*"
```

## Test Dependencies

| Test Type | Requires Redis | Requires Build |
|-----------|----------------|----------------|
| C++ unit tests | Some (concat, scheduler) | Yes |
| TS unit tests | No | Yes (dsl build) |
| Integration tests | Yes (most) | Yes |
| E2E plans | Yes | Yes |
