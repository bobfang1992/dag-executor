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
registry/           # TOML definitions (keys, params, features, tasks, endpoints.{dev,test,prod})
dsl/packages/
  runtime/          # TS DSL runtime APIs for plan authors
  compiler/         # dslc CLI (QuickJS-based sandbox)
  generated/        # Auto-generated keys/params/features/tasks types + task impls
engine/             # C++23 execution engine
plans/              # Official plans (CI, production) → artifacts/plans/
examples/plans/     # Example/tutorial plans → artifacts/plans-examples/
artifacts/          # Compiled JSON artifacts
docs/               # Developer guides
plan-globals.d.ts   # Generated: declares Key/P/EP/coalesce globals for editor support
tsconfig.json       # Root TS config for plan files
```

### Documentation

| Guide | Audience | Purpose |
|-------|----------|---------|
| [TASK_IMPLEMENTATION_GUIDE.md](docs/TASK_IMPLEMENTATION_GUIDE.md) | Infra engineers | How to create C++ tasks |
| [PLAN_AUTHORING_GUIDE.md](docs/PLAN_AUTHORING_GUIDE.md) | Ranking engineers | How to write plans in TS |
| [PLAN_COMPILER_GUIDE.md](docs/PLAN_COMPILER_GUIDE.md) | All | Compiler internals |
| [ADDING_CAPABILITIES.md](docs/ADDING_CAPABILITIES.md) | All | How to add IR capabilities |
| [CAPABILITY_EXAMPLES.md](docs/CAPABILITY_EXAMPLES.md) | All | Capability payload examples |

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

### C++ TaskSpec is SSOT for Task Definitions

Task parameters, types, and output patterns are defined in C++ TaskSpec. TypeScript option types are generated from this source of truth.

**Workflow for TaskSpec changes:**
```bash
# 1. Modify C++ TaskSpec in engine/src/tasks/*.cpp
# 2. Rebuild engine
cmake --build engine/build --parallel

# 3. Regenerate tasks.toml from C++
engine/bin/rankd --print-task-manifest > registry/tasks.toml

# 4. Regenerate TypeScript
pnpm run gen

# 5. Commit all changes
```

**CI verification:**
- `tasks.toml` must match C++ `--print-task-manifest` output
- `TASK_MANIFEST_DIGEST` must match between TS and C++
- `gen:check` verifies `tasks.ts` is in sync with `tasks.toml`

| Component | Location | Role |
|-----------|----------|------|
| C++ TaskSpec (SSOT) | `engine/src/tasks/*.cpp` | Defines task params, types, reads/writes |
| Task manifest | `registry/tasks.toml` | Generated from C++, human-readable, committed |
| Generated types | `dsl/packages/generated/tasks.ts` | Option interfaces for plan authoring |
| Generated impls | `dsl/packages/generated/task-impl.ts` | Task method implementations (validation, node creation) |
| Plan API | `dsl/packages/runtime/src/plan.ts` | PlanCtx/CandidateSet wrappers (delegates to generated impls) |

### Plan Import Restrictions

Plans may only import from:
1. `@ranking-dsl/runtime` - DSL runtime APIs (E, Pred, definePlan)
2. Fragments - Formal reusable subgraphs (when implemented)

**Global Tokens (no import needed):**
- `Key` - Column key references (e.g., `Key.final_score`)
- `P` - Parameter references (e.g., `P.media_age_penalty_weight`)
- `EP` - Endpoint references (e.g., `EP.redis.redis_default`)
- `coalesce` - Null fallback function (e.g., `coalesce(P.x, 0.2)`)
- `regex` - Regex predicate function (e.g., `regex(Key.title, "^test")`)

The compiler injects these via esbuild's `inject` option. **Do not import them.**

**No arbitrary shared helpers.** All reusable code must go through the fragment system.

**Enforcement:**
- esbuild plugin in `bundler.ts` rejects at compile time
- ESLint rule `@ranking-dsl/plan-restricted-imports` catches in editor
- ESLint rule `@ranking-dsl/no-dsl-import-alias` rejects Key/P/EP/coalesce imports
- ESLint rule `@ranking-dsl/no-dsl-reassign` rejects reassignment (`const JK = Key`)

### AST Extraction Limitations

Natural expression syntax (e.g., `vm({ expr: Key.x * coalesce(P.y, 0.2) })`) is extracted at compile time.

**Limitations:**

1. **dslc compiler required**: Natural expressions require the `dslc` compiler (QuickJS-based).

2. **Inline expressions only**: The `expr` value must be an inline expression in the task call. Variables, shorthand, or spread patterns are NOT extracted:
   ```typescript
   // WORKS - inline expression (Key, P, EP, coalesce are globals)
   c.vm({ outKey: Key.x, expr: Key.id * coalesce(P.y, 0.2) })

   // DOES NOT WORK - variable reference
   const myExpr = Key.id * coalesce(P.y, 0.2);
   c.vm({ outKey: Key.x, expr: myExpr })  // Fails at runtime!

   // DOES NOT WORK - shorthand
   const expr = Key.id * coalesce(P.y, 0.2);
   c.vm({ outKey: Key.x, expr })  // Fails at runtime!
   ```

3. **Fragments**: When implemented, fragments must use builder-style expressions OR extraction must be extended to process them.

4. **No reassignment**: Extraction only recognizes exact identifiers `Key`, `P`, `EP`, `coalesce`. Reassigning is not supported:
   ```typescript
   // WORKS - use globals directly (no import needed)
   c.vm({ expr: Key.id * coalesce(P.weight, 0.2) })

   // DOES NOT WORK - reassignment not recognized
   const JK = Key;
   c.vm({ expr: JK.id * coalesce(P.weight, 0.2) })  // Fails!
   ```

**ESLint enforcement:** `@ranking-dsl/eslint-plugin` catches these issues in editor:
- `@ranking-dsl/no-dsl-import-alias` - rejects importing Key/P/EP/coalesce (they are globals)
- `@ranking-dsl/no-dsl-reassign` - rejects reassignment (`const JK = Key`)
- `@ranking-dsl/inline-expr-only` - rejects variable references in expr
- `@ranking-dsl/plan-restricted-imports` - restricts imports in .plan.ts files

**Future work:**
- **Variable resolution**: Allow composing expressions via variables:
  ```typescript
  const score1 = Key.key1 + Key.key2;
  const score2 = Key.key3 * P.weight;
  c.vm({ outKey: Key.final, expr: score1 + score2 });
  ```
  Requires AST-level constant folding to resolve variable references to their definitions.
- **Fragment extraction**: Extend extraction to process fragments when implemented.

## Plan Compilation (dslc)

The `dslc` compiler uses QuickJS for sandboxed, deterministic plan compilation:

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
// Key, P, EP, coalesce are globals (no import needed)
row.get(Key.some_key)      // Standard access via tokens
row.set(Key.some_key, val) // Returns new RowSet (pure functional)
row._debug.get("key")      // Debug-only string access
```

### ExprIR (vm expressions)

**Expression Types** (defined in `@ranking-dsl/generated`):
- `ExprNode` - Builder-style expression tree (e.g., `E.key(Key.x)`)
- `ExprPlaceholder` - Compile-time placeholder for natural syntax
- `ExprInput = ExprNode | ExprPlaceholder` - Union accepted by `vm()` task

**Natural syntax** (preferred, AST-extracted at compile time):
```typescript
c.vm({
  outKey: Key.final_score,
  expr: Key.model_score_1 + Key.model_score_2 * 3 - Key.media_age * P.weight,
  trace: "vm_final",
});
```

**Builder syntax** (explicit, no AST extraction needed):
```typescript
c.vm({
  outKey: Key.final_score,
  expr: E.sub(E.add(E.key(Key.model_score_1), E.mul(E.key(Key.model_score_2), E.const(3))),
              E.mul(E.key(Key.media_age), E.param(P.weight))),
  trace: "vm_final",
});

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
- **Source**: `ctx.viewer()` - returns single row with viewer's user_id and country
- **Transform (fan-out)**: `cs.follow()`, `cs.recommendation()`, `cs.media()` - expand input rows to related items
- **Composition**: `a.concat({ rhs: b })` (uses NodeRef param)
- **Feature/Model**: `fetch_features()`, `call_models()`
- **Transform**: `vm()`, `filter()`, `dedupe()`, `sort()`, `take()`, `extract_features()`
- **Join**: `lhs.join(rhs, { how, by, select, map })`

## HTTP API

```
POST /rank
{
  "request_id": "...",
  "user_id": 123,
  "plan": "reels_plan_a",
  "fragment_versions": { "esr": "v1" },
  "param_overrides": { "media_age_penalty_weight": 0.35 },
  "output_keys": ["id", "final_score"]
}
```

**Required fields:**
- `user_id` - Positive uint32 (1 to 4294967295). Accepts integer or numeric string.

**Optional fields:**
- `request_id` - String. Auto-generated if missing.
- `plan` - Plan name to execute.
- `param_overrides` - Override param values.
- `output_keys` - Keys to include in output.

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
| viewer | ✅ | ✅ `ctx.viewer()` | Source: Redis HGETALL user:{uid}, returns country |
| follow | ✅ | ✅ `.follow()` | Transform: fan-out per input row, hydrates country |
| recommendation | ✅ | ✅ `.recommendation()` | Transform: fan-out per input row, hydrates country |
| media | ✅ | ✅ `.media()` | Transform: Redis LRANGE media:{id} per input |
| vm | ✅ | ✅ `.vm()` | Expression evaluation |
| filter | ✅ | ✅ `.filter()` | Predicate evaluation |
| take | ✅ | ✅ `.take()` | |
| concat | ✅ | ✅ `.concat()` | |
| sort | ✅ | ✅ `.sort()` | Updates permutation only (no materialization) |
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
echo '{"request_id": "test", "user_id": 1}' | engine/bin/rankd

# Run rankd with plan by name (recommended)
echo '{"request_id": "test", "user_id": 1}' | engine/bin/rankd --plan_name reels_plan_a

# Run rankd with plan by explicit path
echo '{"request_id": "test", "user_id": 1}' | engine/bin/rankd --plan artifacts/plans/demo.plan.json

# Run with custom plan store directory
echo '{}' | engine/bin/rankd --plan_dir artifacts/plans-examples --plan_name reels_plan_a

# List available plans
engine/bin/rankd --list-plans

# Print registry digests
engine/bin/rankd --print-registry

# Print task manifest TOML (for regenerating tasks.toml)
engine/bin/rankd --print-task-manifest

# Print plan info (capabilities, writes_eval per node)
engine/bin/rankd --print-plan-info --plan_name reels_plan_a

# Run with schema delta trace (runtime audit)
echo '{"request_id": "test", "user_id": 1}' | engine/bin/rankd --plan_name reels_plan_a --dump-run-trace

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

# Advanced: Direct invocation
node dsl/packages/compiler/dist/cli.js build plans/foo.plan.ts --out artifacts/plans

# Custom manifest and output directory
tsx dsl/tools/build_all_plans.ts --manifest custom.json --out custom/dir

# Visualizer e2e tests (NOT run in CI - run locally when modifying visualizer)
pnpm -C tools/visualizer run test
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

## Capabilities and Extensions (RFC 0001)

The system uses a capability-gated extension mechanism for evolving the Plan/Fragment JSON IR.

### Key Concepts

- **Capability ID**: Stable string like `cap.rfc.NNNN.<slug>.vK`
- **`capabilities_required`**: Sorted, unique list of capabilities a plan needs
- **`extensions`**: Map from capability ID to RFC-defined payload
- **Fail-closed**: Unknown capabilities cause plan rejection
- **Single source of truth**: Capabilities defined in `registry/capabilities.toml`

### Adding New Capabilities

See [docs/ADDING_CAPABILITIES.md](docs/ADDING_CAPABILITIES.md) for the full workflow.

Quick summary:
1. Write RFC with `capability_id` in frontmatter
2. Add capability to `registry/capabilities.toml` with payload schema
3. Run `pnpm -C dsl run gen` to regenerate TS + C++
4. Add tests and update CI

### Capability Registry (TOML)

Capabilities are defined in `registry/capabilities.toml`:

```toml
[[capability]]
id = "cap.rfc.0001.extensions_capabilities.v1"
rfc = "0001"
name = "extensions_capabilities"
status = "implemented"    # implemented | draft | deprecated | blocked
doc = "Base extensions/capabilities mechanism for IR evolution"
payload_schema = '''
{
  "type": "object",
  "additionalProperties": false
}
'''
```

Codegen generates:
- `dsl/packages/generated/capabilities.ts` - TS registry + `validatePayload()`
- `engine/include/capability_registry_gen.h` - C++ constexpr metadata
- `plan-globals.d.ts` - Global type declarations for Key/P/EP/coalesce (editor support)

### Registered Capabilities

| RFC | Capability ID | Status |
|-----|---------------|--------|
| 0001 | `cap.rfc.0001.extensions_capabilities.v1` | implemented |
| 0005 | `cap.rfc.0005.writes_effect.v1` | implemented |

See [docs/CAPABILITY_EXAMPLES.md](docs/CAPABILITY_EXAMPLES.md) for payload examples.

### Key Files

| Component | Location |
|-----------|----------|
| Capability registry (TOML) | `registry/capabilities.toml` |
| Generated TS module | `dsl/packages/generated/capabilities.ts` |
| Generated C++ header | `engine/include/capability_registry_gen.h` |
| C++ runtime logic | `engine/src/capability_registry.cpp` |
| DSL runtime | `dsl/packages/runtime/src/plan.ts` |
| Artifact validation | `dsl/packages/runtime/src/artifact-validation.ts` |
| writes_effect ADT (C++) | `engine/include/writes_effect.h` |
| writes_effect evaluator (C++) | `engine/src/writes_effect.cpp` |
| writes_effect (TS) | `dsl/packages/runtime/src/writes-effect.ts` |
| Schema delta (runtime audit) | `engine/include/schema_delta.h` |

### Digest Computation

**Capability Registry Digest**: `sha256(canonical_json({schema_version, entries}))`
- TS: `CAPABILITY_REGISTRY_DIGEST` from `@ranking-dsl/generated`
- C++: `kCapabilityRegistryDigest` from `capability_registry_gen.h`
- Engine: `engine/bin/rankd --print-registry`

**Plan Capabilities Digest**: `sha256(canonical_json({capabilities_required, extensions}))`
- Computed identically in TS and C++ for cross-language parity

---

## Endpoint Registry (Step 14.2)

Governed registry for external service endpoints (Redis, HTTP, etc.) with per-environment configuration.

### Key Concepts

- **Endpoint ID**: String format `ep_NNNN` (e.g., `ep_0001`)
- **Two-digest system**:
  - `endpoint_registry_digest`: Hash of endpoint definitions (id, name, kind) - env-invariant, governance
  - `endpoints_config_digest`: Hash of env-specific config (host, port, policy) - operational
- **Per-env TOML files**: `registry/endpoints.{dev,test,prod}.toml`
- **Cross-env validation**: Same endpoint_ids with same name/kind across all environments
- **Fail-closed**: Only `static` resolver supported; unknown kinds/resolvers rejected

### Endpoint TOML Structure

```toml
# registry/endpoints.dev.toml
version = 1

[[endpoint]]
endpoint_id = "ep_0001"
name = "redis_default"
kind = "redis"

  [endpoint.resolver]
  type = "static"
  host = "127.0.0.1"
  port = 6379

  [endpoint.policy]
  max_inflight = 64
```

### EndpointRef Param Type

Tasks can declare endpoint parameters using `TaskParamType::EndpointRef`:

```cpp
ParamField{
  .name = "cache_endpoint",
  .type = TaskParamType::EndpointRef,
  .required = true,
  .endpoint_kind = EndpointKind::Redis  // Optional: constrain to specific kind
}
```

In TypeScript:
```typescript
import { EP } from "@ranking-dsl/generated";

// EP is grouped by kind with branded EndpointId type
ctx.someTask({ endpoint: EP.redis.redis_default });
```

### Engine Flags

```bash
# Run with specific environment (default: dev)
echo '{"user_id": 1}' | engine/bin/rankd --env prod --plan_name my_plan

# Custom artifacts directory (default: artifacts)
engine/bin/rankd --artifacts_dir /path/to/artifacts --env test

# Print registry shows endpoint digests
engine/bin/rankd --print-registry
# Output includes: endpoint_registry_digest, endpoints_config_digest, num_endpoints
```

### Key Files

| Component | Location |
|-----------|----------|
| Dev endpoints | `registry/endpoints.dev.toml` |
| Test endpoints | `registry/endpoints.test.toml` |
| Prod endpoints | `registry/endpoints.prod.toml` |
| Generated TS | `dsl/packages/generated/endpoints.ts` |
| Generated JSON (per-env) | `artifacts/endpoints.{dev,test,prod}.json` |
| C++ registry | `engine/include/endpoint_registry.h` |
| C++ tests | `engine/tests/test_endpoint_registry.cpp` |

---

## Implementation Progress

See [docs/IMPLEMENTATION_PROGRESS.md](docs/IMPLEMENTATION_PROGRESS.md) for detailed step-by-step implementation history.

### Quick Status

**✅ Completed:**
- Steps 00-14.4: Core engine, registries, codegen, DSL runtime, QuickJS compiler
- Tasks: viewer, follow, media, recommendation (Redis-backed), vm, filter, take, concat, sort
- Capabilities system (RFC 0001), writes_effect (RFC 0005)
- AST extraction for natural expressions and predicates
- Visualizer with live plan editing, registry browser, and edit existing plan support
- Endpoint Registry with per-env config, EndpointRef param type
- Local Redis harness (Docker Compose + seed script)

**🔲 Remaining:**
- Fragment authoring, budget enforcement, HTTP server
- Tasks: fetch_features, call_models, dedupe, join, extract_features
- Audit logging, SourceRef generation

