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

---

## 🔲 Not Yet Implemented

### Step 14.3: Endpoint Registry Hardening
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
