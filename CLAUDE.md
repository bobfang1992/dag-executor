# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **Ranking DSL + Engine** project implementing a governed, type-safe domain-specific language for building ranking pipelines. The system separates infra engineers (who implement Tasks in C++) from ranking engineers (who compose Plans/Fragments in TypeScript).

## Architecture

### Two-Layer System
1. **DSL Layer (TypeScript)**: Authoring surface for ranking engineers
   - Plans (`*.plan.ts`) → compiled to `artifacts/plans/*.plan.json`
   - Fragments (`*.fragment.ts`) → compiled to `artifacts/fragments/<name>/<version>.fragment.json`

2. **Engine Layer (C++23)**: Execution runtime
   - Columnar/SoA data model with SelectionVector (no eager materialization)
   - Dictionary-encoded strings
   - HTTP server exposing `POST /rank`

### Compilation Pipeline
```
TypeScript (.plan.ts)
    ↓ esbuild (bundle to single IIFE)
Bundled JavaScript
    ↓ QuickJS sandbox execution
    ↓ definePlan() → __emitPlan()
JSON Plan Artifact
    ↓ Engine loads + validates
C++ Execution
```

The `dslc` compiler (Step 10):
1. Bundles plan + DSL runtime + generated tokens with esbuild (single IIFE, no Node builtins)
2. Executes bundle in QuickJS sandbox (no eval, no Function, no process, no imports)
3. Plan calls `definePlan()` which detects sandbox and calls `__emitPlan(artifact)`
4. Compiler captures artifact and validates (no undefined, no functions, no cycles)
5. Outputs deterministic JSON artifact with stable key ordering

Security: QuickJS sandbox disables eval, Function constructor, and all Node globals (process, require, module, fs, network).

### Repository Layout
```
registry/           # TOML definitions (keys.toml, params.toml, features.toml)
dsl/packages/
  runtime/          # TS DSL runtime APIs for plan authors
  compiler/         # dslc CLI
  generated/        # Auto-generated keys/params/features/task bindings
engine/             # C++23 execution engine
plans/              # Official plans (CI, production) → artifacts/plans/
examples/plans/     # Example/tutorial plans → artifacts/plans-examples/
artifacts/          # Compiled JSON artifacts
```

## Key Design Constraints

### No `undefined`
DSL must not produce or pass `undefined` anywhere. Only `null` is allowed for missing values. Compiler/bindings/runtime enforce this.

### No `any`
Use `unknown` for untyped inputs and validate into typed structures. ESLint enforces `@typescript-eslint/no-explicit-any: "error"`.

### Fail-Closed Governance
- All entity fields pre-declared in Key Registry
- Tasks declare reads/writes (auditable dataflow)
- Engine rejects invalid plans and refuses unsafe writes
- No node may write `Key.id`

### Registry Rules
- Keys, Params, and Features are append-only (no deletion)
- Each has stable, non-reusable numeric IDs
- Lifecycle: `active` → `deprecated` → `blocked`
- Renames require new ID + deprecate old

## Plan Compilation (dslc)

Two compilers are available. **QuickJS-based (dslc)** is the primary compiler used in CI.

### QuickJS Compiler (Primary)

Uses QuickJS for sandboxed, deterministic plan compilation:

**Security Model:**
- **No eval/Function**: Dynamic code generation is blocked
- **No Node globals**: No access to process, require, module, fs, network
- **No dynamic imports**: All code must be statically bundled
- **Artifact validation**: Ensures no undefined, functions, symbols, or cycles

**Usage:**
```bash
# Compile single plan
pnpm run dslc build examples/plans/reels_plan_a.plan.ts --out artifacts/plans

# Compile all plans (manifest-based, default for CI)
pnpm run plan:build:all
```

**How It Works:**
1. **Bundle**: esbuild combines plan + runtime + generated tokens → single IIFE
2. **Execute**: QuickJS runs bundle in sandbox, plan calls `definePlan()`
3. **Emit**: `definePlan()` detects sandbox via `global.__emitPlan` and emits artifact
4. **Validate**: Compiler validates artifact structure and JSON-serializability
5. **Write**: Deterministic JSON with stable key ordering + built_by metadata

**Error Handling:**
Plans that attempt forbidden operations fail with clear errors:
```bash
$ pnpm run dslc build test/fixtures/plans/evil.plan.ts --out artifacts/plans
Error: QuickJS execution failed for evil.plan.ts: not a function
```

### Node Compiler (Legacy/Fallback)

Uses Node.js for fast iteration during development:

**Usage:**
```bash
# Single plan
pnpm run plan:build:node examples/plans/my_plan.plan.ts --out artifacts/plans

# All plans (Node backend)
pnpm run plan:build:all:node
```

**Use for:** Debugging, development iteration. **Not for CI.**

### Plan Store Model

The project uses a central plan store model with two directories:

| Directory | Purpose | Compiled To |
|-----------|---------|-------------|
| `plans/` | Official plans (CI, production) | `artifacts/plans/` |
| `examples/plans/` | Example/tutorial plans | `artifacts/plans-examples/` |

Each store has:
- `manifest.json` - List of plans to compile (committed, source of truth)
- `index.json` - Generated index with plan names, digests, and build metadata

Example `plans/manifest.json`:
```json
{
  "schema_version": 1,
  "plans": [
    "plans/concat_plan.plan.ts",
    "plans/reels_plan_a.plan.ts",
    "plans/regex_plan.plan.ts"
  ]
}
```

**To add a plan:** Add its path to the `plans` array, or run `pnpm run plan:manifest:sync`.

See [docs/PLAN_COMPILER_GUIDE.md](docs/PLAN_COMPILER_GUIDE.md) for detailed usage.

## Core Concepts

### Key Access
```typescript
row.get(Key.some_key)      // Standard access via tokens
row.set(Key.some_key, val) // Returns new RowSet (pure functional)
row._debug.get("key")      // Debug-only string access
```

### ExprIR (vm expressions)
```typescript
c.vm(Key.final_score,
     Key.model_score_1 + Key.model_score_2 * 3 - Key.media_age * P.media_age_penalty_weight,
     { trace: "vm_final" });
```

### PredIR (filter predicates)
```typescript
c.filter(Key.country.in(["US","CA"]) && Key.esr_score > P.esr_cutoff, { trace: "policy" });
```

### Fragments
Versioned, reusable subgraphs with optional arguments:
```typescript
export const esr = defineFragment({
  name: "esr",
  kind: "transform",
  versions: { v0: (c, ctx) => ..., v1: (c, ctx) => ... },
  default: "v0",
  limits: { maxNodes: 3000, maxDepth: 300 }
});
```

## Engine Execution Model

### Data Structures
- **ColumnBatch**: SoA storage with typed columns + validity bitsets
- **RowSet**: `base_batch` + optional `SelectionVector` + optional `PermutationVector`
- **StringDictColumn**: Dictionary-encoded strings for regex optimization

### Task Categories
- **Source**: `viewer.follow()`, `viewer.fetch_cached_recommendation()`
- **Composition**: `concat(a, b)`
- **Feature/Model**: `fetch_features()`, `call_models()`
- **Transform**: `vm()`, `filter()`, `dedupe()`, `sort()`, `take()`, `extract_features()`
- **Join**: `lhs.join(rhs, { how, by, select, map })`

## HTTP API

```
POST /rank
{
  "request_id": "...",
  "plan": "reels_plan_a",
  "fragment_versions": { "esr": "v1" },
  "param_overrides": { "media_age_penalty_weight": 0.35 },
  "output_keys": ["id", "final_score"]
}
```

## Complexity Budgets

Enforced at compile time per artifact:
- `max_nodes`, `max_edges`, `max_depth`, `max_fanout`, `max_fanin`

## Audit Logging

Three log streams (JSON Lines):
- **Request-level**: `request_id`, `plan`, `fragment_versions`, `status`, `latency_ms`
- **Task-level**: `request_id`, `node_id`, `op`, timing, `trace`
- **Param-level**: `request_id`, `param_name`, `value`, `origin`

## SourceRef for Debugging

Compiled artifacts include source mapping tables (`source_files`, `source_spans`) so runtime errors can reference original TypeScript locations.

---

## Feature Parity (Engine vs DSL)

### Expression Ops (ExprIR)
| Op | Engine | DSL | Notes |
|----|--------|-----|-------|
| const_number | ✅ | ✅ `E.const()` | |
| const_null | ✅ | ✅ `E.constNull()` | |
| key_ref | ✅ | ✅ `E.key()` | Float columns only |
| param_ref | ✅ | ✅ `E.param()` | |
| add | ✅ | ✅ `E.add()` | |
| sub | ✅ | ✅ `E.sub()` | |
| mul | ✅ | ✅ `E.mul()` | |
| neg | ✅ | ✅ `E.neg()` | |
| coalesce | ✅ | ✅ `E.coalesce()` | Null fallback |
| div | ❌ | ❌ | Not implemented |

### Predicate Ops (PredIR)
| Op | Engine | DSL | Notes |
|----|--------|-----|-------|
| const_bool | ✅ | ✅ `Pred.constBool()` | |
| and | ✅ | ✅ `Pred.and()` | |
| or | ✅ | ✅ `Pred.or()` | |
| not | ✅ | ✅ `Pred.not()` | |
| cmp | ✅ | ✅ `Pred.cmp()` | ==, !=, <, <=, >, >= |
| in (numeric) | ✅ | ✅ `Pred.in()` | Homogeneous list |
| in (string) | ✅ | ✅ `Pred.in()` | Dictionary lookup |
| is_null | ✅ | ✅ `Pred.isNull()` | |
| not_null | ✅ | ✅ `Pred.notNull()` | |
| regex | ✅ | ✅ `Pred.regex()` | RE2, dict optimization |

### Tasks
| Task | Engine | DSL | Notes |
|------|--------|-----|-------|
| viewer.follow | ✅ | ✅ `ctx.viewer.follow()` | |
| viewer.fetch_cached_recommendation | ✅ | ✅ `ctx.viewer.fetch_cached_recommendation()` | |
| vm | ✅ | ✅ `.vm()` | Expression evaluation |
| filter | ✅ | ✅ `.filter()` | Predicate evaluation |
| take | ✅ | ✅ `.take()` | |
| concat | ✅ | ✅ `.concat()` | |
| sort | ❌ | ❌ | Not implemented |
| dedupe | ❌ | ❌ | Not implemented |
| join | ❌ | ❌ | Not implemented |

### Column Types
| Type | Engine | DSL | Notes |
|------|--------|-----|-------|
| id (int64) | ✅ | ✅ | Read-only |
| float | ✅ | ✅ | Via vm task |
| string | ✅ | ✅ | Dictionary-encoded |

---

## Build Commands

```bash
# Build engine
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel

# Run RowSet unit tests
engine/bin/rowset_tests

# Run rankd (Step 00 fallback - no plan)
echo '{"request_id": "test"}' | engine/bin/rankd

# Run rankd with plan by name (recommended)
echo '{"request_id": "test"}' | engine/bin/rankd --plan_name reels_plan_a

# Run rankd with plan by explicit path
echo '{"request_id": "test"}' | engine/bin/rankd --plan artifacts/plans/demo.plan.json

# Run with custom plan store directory
echo '{}' | engine/bin/rankd --plan_dir artifacts/plans-examples --plan_name reels_plan_a

# List available plans
engine/bin/rankd --list-plans

# Print registry digests
engine/bin/rankd --print-registry

# Run all CI tests
./scripts/ci.sh

# DSL commands
pnpm -C dsl install    # Install DSL dependencies
pnpm -C dsl run lint   # Lint TypeScript
pnpm -C dsl run gen    # Run codegen (regenerate all outputs)
pnpm -C dsl run gen:check  # Verify generated outputs are up-to-date

# Full build (gen + build DSL + compile all plans with dslc)
pnpm run build

# Or step by step:
pnpm run gen                      # Regenerate registry tokens
pnpm run build:dsl                # Build DSL packages (including dslc compiler)
pnpm run plan:build:all           # Compile official plans (plans/ → artifacts/plans/)
pnpm run plan:build:examples      # Compile example plans (examples/plans/ → artifacts/plans-examples/)

# Compile single plan with dslc (QuickJS sandbox)
pnpm run dslc build plans/my_plan.plan.ts --out artifacts/plans

# Sync manifests from directory scan
pnpm run plan:manifest:sync           # Sync plans/manifest.json
pnpm run plan:manifest:sync:examples  # Sync examples/plans/manifest.json

# Compile all plans with Node backend (debugging/fallback)
pnpm run plan:build:all:node

# Fallback: Node-based compiler (legacy, for development)
pnpm run plan:build:node plans/foo.plan.ts --out artifacts/plans

# Advanced: Direct invocation
node dsl/packages/compiler/dist/cli.js build plans/foo.plan.ts --out artifacts/plans
tsx dsl/packages/compiler-node/src/cli.ts plans/foo.plan.ts --out artifacts/plans

# Custom manifest and output directory
tsx dsl/tools/build_all_plans.ts --manifest custom.json --out custom/dir --backend quickjs
```

---

## PR Creation

Use `--body-file` instead of inline heredocs to avoid polluting `.claude/settings.local.json` with complex command patterns:

```bash
# Write PR body to temp file
cat > /tmp/pr-body.md <<'EOF'
## Summary
- ...

## Test plan
- [x] ...

🤖 Generated with [Claude Code](https://claude.ai/code)
EOF

# Create PR using --body-file
gh pr create --title "Step XX: Feature Name" --body-file /tmp/pr-body.md
```

## Codex Review

Codex (OpenAI's GitHub code review bot) is enabled for this repo. To request a review:

```bash
# Tag Codex on an existing PR
gh pr comment <PR_NUMBER> --body "@codex review"

# With context about what changed
gh pr comment 17 --body "@codex review

Latest changes:
- Fixed index generation
- Added new tests"
```

Codex will post inline comments with severity labels (P0/P1/P2). Address P0/P1 findings before merging.

**Important:** Always tag `@codex review` after:
- Creating a new PR
- Pushing updates to an existing PR

---

## Implementation Progress

### ✅ Completed

**Step 00: Minimal Engine Skeleton**
- `engine/src/main.cpp` - rankd binary reading JSON from stdin, writing to stdout
- `engine/CMakeLists.txt` - C++23 build with nlohmann/json
- `scripts/ci.sh` - Build + smoke test gate
- Returns 5 synthetic candidates (ids 1-5)
- Handles request_id (echo or generate) + engine_request_id

**Step 01: Plan Loading + DAG Execution**
- `--plan <path>` CLI argument for rankd (using CLI11)
- `engine/include/plan.h` + `engine/src/plan.cpp` - Plan/Node structs, JSON parsing
- `engine/include/executor.h` + `engine/src/executor.cpp` - Validation + topo sort execution
- `engine/include/task_registry.h` + `engine/src/task_registry.cpp` - Task registry with `viewer.follow` and `take`
- Fail-closed validation: schema_version, unique node_ids, valid inputs, known ops, cycle detection
- Test artifacts: `demo.plan.json`, `cycle.plan.json`, `missing_input.plan.json`

**Step 02: Columnar RowSet Model**
- `engine/include/column_batch.h` - ColumnBatch with id column + validity + DebugCounters
- `engine/include/rowset.h` - RowSet with batch/selection/order + materializeIndexViewForOutput()
- `take` shares batch pointer (no column copy), materialize_count stays 0
- Iteration semantics: order > selection > [0..N), with order+selection filtering
- `engine/tests/test_rowset.cpp` - Unit tests for RowSet iteration and take behavior

**Step 03: Registries + Codegen**
- `registry/keys.toml` - Key Registry with 8 keys (id, model_score_1/2, final_score, country, title, features_esr/lsr)
- `registry/params.toml` - Param Registry with 3 params (media_age_penalty_weight, blocklist_regex, esr_cutoff)
- `registry/features.toml` - Feature Registry with 2 features (esr.country, esr.media_age_hours)
- `dsl/` - TypeScript codegen tool with strict typing (no `any`)
- `dsl/src/codegen.ts` - Generates TS tokens, C++ headers, and JSON artifacts with SHA-256 digests
- Generated outputs:
  - `dsl/src/generated/keys.ts`, `params.ts`, `features.ts` - TS tokens (Key, P, Feat)
  - `engine/include/key_registry.h`, `param_registry.h`, `feature_registry.h` - C++ enums + metadata
  - `artifacts/keys.json`, `params.json`, `features.json` - JSON artifacts with digests
- `--print-registry` flag for rankd to output registry digests
- CI runs `pnpm -C dsl run gen:check` to verify generated outputs are up-to-date

**Step 04: TaskSpec Validation**
- `engine/include/task_registry.h` - TaskSpec, ParamField, ValidatedParams types
- `engine/include/sha256.h` - Header-only SHA256 for digest computation
- TaskSpec as single source of truth for task validation
- Strict fail-closed validation: missing required params, wrong types, unexpected fields
- `task_manifest_digest` computed via SHA256 of canonical TaskSpec JSON
- `--print-registry` extended with `task_manifest_digest` and `num_tasks`
- Negative test plan artifacts: `bad_type_fanout.plan.json`, `missing_fanout.plan.json`, `extra_param.plan.json`, `bad_trace_type.plan.json`
- CI tests for all negative param validation cases

**Step 05a: ParamTable and param_overrides Validation**
- `engine/include/param_table.h` - ParamTable class with typed getters, validation helpers, ExecCtx
- Request-level `param_overrides` validation against registry metadata
- Fail-closed semantics: unknown params, non-writable, deprecated/blocked, wrong types, non-finite floats
- Nullable param handling per registry metadata
- Catch2 testing framework integration (14 test cases, 63 assertions)

**Step 05b: vm Task and Expression Evaluation**
- `engine/include/plan.h` - ExprNode struct for recursive expression trees
- `engine/src/plan.cpp` - parse_expr_node() for expr_table parsing
- `engine/include/expr_eval.h` - Expression evaluation with null propagation
- `engine/include/column_batch.h` - FloatColumn support, shared id storage via shared_ptr
- `vm` task: evaluates expressions per row, writes float columns
- ExprNode ops: const_number, const_null, key_ref, param_ref, add, sub, mul, neg, coalesce
- Demo plan computes: `final_score = id * coalesce(P.media_age_penalty_weight, 0.2)`

**Step 06: filter Task and Predicate Evaluation**
- `engine/include/plan.h` - PredNode struct for recursive predicate trees
- `engine/src/plan.cpp` - parse_pred_node() for pred_table parsing
- `engine/include/pred_eval.h` - Predicate evaluation with null semantics (per spec)
- `engine/tests/test_pred_eval.cpp` - Comprehensive unit tests (22 test cases, 124 assertions)
- `filter` task: evaluates predicates, updates selection without copying columns
- PredNode ops: const_bool, and, or, not, cmp (==,!=,<,<=,>,>=), in, is_null, not_null
- Null semantics: cmp/in with null operands return false (per spec), is_null/not_null for explicit null testing
- Demo plan filters: `final_score >= 0.6`
- Negative test plans: missing_pred_id, unknown_pred_id, bad_pred_table_shape, bad_in_list

**Step 07: StringDictColumn, concat task, and output contracts**
- `engine/include/column_batch.h` - StringDictColumn with dictionary-encoded strings
- `concat` task: concatenates two RowSets with schema validation
- Output contracts: validates output_keys against available columns
- Plan artifacts: `concat_demo.plan.json`

**Step 08: Regex PredIR with Dictionary Optimization**
- `engine/CMakeLists.txt` - RE2 dependency (2022-04-01)
- `engine/include/param_table.h` - ExecStats struct for regex_re2_calls tracking
- `engine/include/plan.h` - PredNode regex fields (regex_key_id, regex_pattern, regex_param_id, regex_flags)
- `engine/include/pred_eval.h` - Regex evaluation with thread_local cache, dict-scan optimization
- `engine/tests/test_regex.cpp` - Simple main test binary (not Catch2)
- Dict-scan optimization: regex runs once per dict entry (O(dict_size)), lookup via codes (O(1))
- Plan artifacts: `regex_demo.plan.json`, `regex_param_demo.plan.json`, `bad_regex_flags.plan.json`

**Step 09: Node-based Plan Authoring (TypeScript DSL)**
- `dsl/packages/runtime/` - TypeScript runtime package
  - `plan.ts` - PlanCtx, CandidateSet, definePlan() for node-based plan authoring
  - `expr.ts` - E builder: const, constNull, key, param, add, sub, mul, neg, coalesce
  - `pred.ts` - Pred builder: constBool, and, or, not, cmp, in, isNull, notNull, regex
  - `guards.ts` - assertNotUndefined, checkNoUndefined helpers
- `dsl/packages/compiler-node/` - Simple CLI compiler (legacy, Node-based)
  - `cli.ts` - Compiles `*.plan.ts` → `artifacts/plans/*.plan.json`
  - `stable-stringify.ts` - Deterministic JSON serialization
- `dsl/packages/generated/` - Generated Key/Param/Feature tokens re-exported
- Example plans: `reels_plan_a.plan.ts`, `concat_plan.plan.ts`, `regex_plan.plan.ts`
- Build: `pnpm run build:dsl` then `pnpm run plan:build examples/plans/*.plan.ts`

**Step 10: QuickJS-based Plan Execution**
- `dsl/packages/compiler/` - QuickJS-based dslc compiler (replaces Node-based compiler)
  - `bundler.ts` - esbuild integration: bundles plan + runtime → single IIFE script
  - `executor.ts` - QuickJS sandbox: executes bundle, captures __emitPlan(), validates artifact
  - `cli.ts` - Main dslc CLI: `dslc build <plan.ts> --out <dir>`, full artifact schema validation
  - `stable-stringify.ts` - Deterministic JSON serialization
- Runtime refactoring:
  - `plan.ts`: `definePlan()` detects QuickJS mode via `global.__emitPlan` and emits artifact
  - Maintains backward compatibility with Node-based compiler-node
- Security:
  - Sandbox disables: eval, Function (including prototype bypass), process, require, module, dynamic imports
  - Validates artifacts: no undefined, no functions, no symbols, no cycles (shared refs OK)
  - Full schema validation: schema_version, plan_name, nodes, outputs, node inputs reference existing nodes
- Testing:
  - `test/fixtures/plans/evil.plan.ts` - Attempts eval() → rejected ✅
  - `test/fixtures/plans/evil_proto.plan.ts` - Attempts Function via prototype → rejected ✅
  - `test/fixtures/plans/name_mismatch.plan.ts` - plan_name != filename → rejected ✅
  - CI Tests 32-33: Verify sandbox security
  - CI Test 38: Verify plan_name/filename enforcement
- Build: `pnpm run dslc build <plan.ts> --out artifacts/plans` (default for `pnpm run build`)
- Benefits: Deterministic builds, sandboxed execution, portable (WASM, no native addons)

**Step 10.5: Central Plan Store**
- Plan store model:
  - `plans/` → official plans (CI, production) → `artifacts/plans/`
  - `examples/plans/` → example/tutorial plans → `artifacts/plans-examples/`
  - `test/fixtures/plans/` → negative test cases (not compiled, only for CI tests)
- Each store has `manifest.json` (committed SSOT) and generated `index.json`
- `index.json`: plan names, paths, digests, build metadata for engine lookup
- Engine plan loading by name:
  - `--plan_name <name>` - Load plan by name from plan_dir
  - `--plan_dir <dir>` - Plan store directory (default: `artifacts/plans`)
  - `--list-plans` - List available plans from index.json
- Security: Plan names validated against `[A-Za-z0-9_]+` pattern (no path traversal)
- Compiler enforcement: `plan_name` must match filename (prevents index mismatches)
- Manifest sync tools: `pnpm run plan:manifest:sync`, `pnpm run plan:manifest:sync:examples`
- CI tests: plan store index generation, engine `--list-plans`, `--plan_name` loading, name enforcement

### 🔲 Not Yet Implemented

**Registries (§3)**
- [x] Key Registry (`registry/keys.toml`) + codegen
- [x] Param Registry (`registry/params.toml`) + codegen
- [x] Feature Registry (`registry/features.toml`) + codegen
- [ ] Lifecycle/deprecation enforcement

**DSL Layer (§4-7)**
- [x] TypeScript runtime package (`dsl/packages/runtime`)
- [x] Compiler (`dsl/packages/compiler`, dslc CLI with QuickJS sandbox)
- [x] Legacy Node-based compiler (`dsl/packages/compiler-node`)
- [x] Generated bindings (`dsl/packages/generated`)
- [x] Plan authoring surface (definePlan, CandidateSet)
- [x] ExprIR builder (E.const, E.key, E.param, E.add, E.sub, E.mul, E.neg, E.coalesce)
- [x] PredIR builder (Pred.cmp, Pred.in, Pred.isNull, Pred.notNull, Pred.and, Pred.or, Pred.not, Pred.regex)
- [x] QuickJS-based plan execution with esbuild bundling
- [x] Sandbox security (no eval, no Function, no Node globals)
- [ ] Fragment authoring surface
- [ ] AST extraction for complex expressions (future enhancement)

**Engine Core (§9)**
- [x] ColumnBatch (SoA storage) - id + float columns with validity
- [x] SelectionVector / PermutationVector (order)
- [x] Dictionary-encoded strings (StringDictColumn)
- [x] Task interface + registry with TaskSpec validation
- [x] DAG validation and linking with param validation
- [x] ExprIR evaluation (expr_table)
- [x] PredIR evaluation (pred_table) including regex with dict optimization
- [ ] Budget enforcement

**Tasks (§8)**
- [x] Source tasks: `viewer.follow` (columnar)
- [x] concat (with schema validation)
- [ ] fetch_features / call_models
- [x] vm (expression evaluation, float column output)
- [x] filter (predicate evaluation, selection update, regex support)
- [ ] dedupe / sort
- [x] take (columnar, no-copy)
- [ ] join (left/inner/semi/anti)
- [ ] extract_features

**HTTP Server (§13)**
- [ ] POST /rank endpoint (currently stdin/stdout only)

**Audit Logging (§11)**
- [ ] Request-level logs
- [ ] Task-level logs
- [ ] Param-level logs
- [ ] Critical path tracing

**Tooling (§14)**
- [x] dslc compiler CLI (QuickJS-based, sandboxed execution)
- [x] plan-build CLI (legacy Node-based compiler, available as fallback)
- [x] Plan store with index.json generation
- [x] Manifest sync tools (plan:manifest:sync)
- [x] Engine plan loading by name (--plan_name, --list-plans)
- [ ] Plan skeleton generator (`plan:new` - interactive wizard to scaffold new plans)
- [ ] Task skeleton generator (scaffold new C++ task with header, source, registry entry)
- [ ] SourceRef generation
- [ ] AST extraction for complex expressions (future enhancement)
