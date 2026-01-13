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
TypeScript → AST extraction → ExprIR/PredIR → QuickJS graph builder → JSON IR → C++ Engine
```

The `dslc` compiler:
1. Parses TS and extracts `vm` expressions → ExprIR table
2. Extracts `filter` predicates → PredIR table
3. Rewrites source to use `__expr(id)` / `__pred(id)`
4. Executes in QuickJS to build DAG
5. Outputs JSON artifacts

### Repository Layout
```
registry/           # TOML definitions (keys.toml, params.toml, features.toml)
dsl/packages/
  runtime/          # TS DSL runtime APIs for plan authors
  compiler/         # dslc CLI
  generated/        # Auto-generated keys/params/features/task bindings
engine/             # C++23 execution engine
artifacts/          # Compiled JSON artifacts
examples/           # Example plans and fragments
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

## Build Commands

```bash
# Build engine
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel

# Run RowSet unit tests
engine/bin/rowset_tests

# Run rankd (Step 00 fallback - no plan)
echo '{"request_id": "test"}' | engine/bin/rankd

# Run rankd with plan
echo '{"request_id": "test"}' | engine/bin/rankd --plan artifacts/plans/demo.plan.json

# Print registry digests
engine/bin/rankd --print-registry

# Run all CI tests
./scripts/ci.sh

# DSL commands
npm --prefix dsl install    # Install DSL dependencies
npm --prefix dsl run lint   # Lint TypeScript
npm --prefix dsl run gen    # Run codegen (regenerate all outputs)
npm --prefix dsl run gen:check  # Verify generated outputs are up-to-date
```

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
  - `engine/include/keys.h`, `params.h`, `features.h` - C++ enums + metadata
  - `artifacts/keys.json`, `params.json`, `features.json` - JSON artifacts with digests
- `--print-registry` flag for rankd to output registry digests
- CI runs `npm --prefix dsl run gen:check` to verify generated outputs are up-to-date

### 🔲 Not Yet Implemented

**Registries (§3)**
- [x] Key Registry (`registry/keys.toml`) + codegen
- [x] Param Registry (`registry/params.toml`) + codegen
- [x] Feature Registry (`registry/features.toml`) + codegen
- [ ] Lifecycle/deprecation enforcement

**DSL Layer (§4-7)**
- [ ] TypeScript runtime package (`dsl/packages/runtime`)
- [ ] Compiler (`dsl/packages/compiler`, dslc CLI)
- [ ] Generated bindings (`dsl/packages/generated`)
- [ ] Plan/Fragment authoring surface
- [ ] ExprIR extraction (vm expressions)
- [ ] PredIR extraction (filter predicates)
- [ ] QuickJS graph builder

**Engine Core (§9)**
- [x] ColumnBatch (SoA storage) - id column only for now
- [x] SelectionVector / PermutationVector (order)
- [ ] Dictionary-encoded strings
- [x] Task interface + registry (basic)
- [x] DAG validation and linking (basic)
- [ ] Budget enforcement

**Tasks (§8)**
- [x] Source tasks: `viewer.follow` (columnar)
- [ ] concat
- [ ] fetch_features / call_models
- [ ] vm / filter / dedupe / sort
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
- [ ] dslc compiler CLI
- [ ] SourceRef generation
