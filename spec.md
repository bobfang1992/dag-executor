# Ranking DSL MVP Spec (English)

> Goal (project-level): distill real-world ranking infrastructure experience into a publishable paper.  
> Milestone #1 (engineering): recreate a **minimal, executable MVP** of a governed **Ranking DSL + Engine**.

This document is an **executable implementation spec** intended for Claude Code. It defines:
- The DSL surface (TypeScript authoring)
- Compilation pipeline to JSON IR (QuickJS graph-building + AST-to-IR for expressions)
- Fragment system (versioned reusable subgraphs)
- Registries (keys + params)
- C++23 execution engine (columnar batches, selection vectors, budgets, audit logs)
- HTTP server wrapper and request/response schemas
- MVP scope, tests, and explicit non-goals

---

## 0. Design principles (hard requirements)

1. **Infra vs Ranking separation**
   - Infra engineers implement **Tasks** in C++ and declare their specs.
   - Ranking engineers write **Plans/Fragments** (TypeScript) that compose tasks.

2. **Governance + auditability**
   - All entity fields are **pre-declared** in a central **Key Registry**.
   - Tasks must declare **reads/writes** (auditable dataflow).
   - Engine is **fail-closed**: reject invalid plans and refuse unsafe writes.

3. **Determinism + reproducibility**
   - Plans compile to JSON IR; runtime engine executes JSON only.
   - IR includes `schema_version` and digests to detect mismatches.

4. **Performance-first execution model**
   - Candidate data is **column-oriented** (SoA).
   - Filtering uses **SelectionVector** (no eager materialization).
   - String columns use **dictionary encoding**.

5. **No `undefined`**
   - DSL must not produce or pass `undefined` anywhere.
   - Only `null` is allowed for missing values.
   - Compiler/bindings/runtime enforce this.

---

## 1. Terminology

- **Candidate / Entity**: a row representing one recommendation item. Semantically an immutable key-value bag.
- **Key**: a pre-declared entity field. Accessed via `Key.*` tokens (not raw strings).
- **Param**: a pre-declared tunable parameter. Accessed via `P.*` tokens.
- **Task**: an infra-provided primitive operation (C++). Instantiated as a node in the DAG.
- **Plan**: top-level ranking pipeline authored in TS, compiled to a DAG JSON.
- **Fragment**: reusable, versioned subgraph authored in TS; plan composes fragments.
- **RowSet**: runtime value representing a candidate batch + selection/order state.
- **SelectionVector**: array of row indices representing currently active rows.
- **Permutation**: an order vector used by `sort` without physically permuting columns.
- **ExprIR**: arithmetic expression IR used by `vm`.
- **PredIR**: boolean predicate IR used by `filter`.

---

## 2. Repository layout (monorepo)

```
repo/
  registry/
    keys.toml
    params.toml
    features.toml
  dsl/
    packages/
      runtime/          # TS DSL runtime APIs used by plan authors
      compiler/         # dslc CLI: TS AST passes + QuickJS graph builder
      generated/        # auto-generated keys/params/features/task bindings
  engine/
    include/
    src/
    tests/
  artifacts/
    plans/              # compiled plan JSON artifacts
    fragments/          # compiled fragment JSON artifacts
  examples/
    plans/
    fragments/
  tools/
    dslc.ts             # entrypoint wrapper (optional)
```

---

## 3. Registries

### 3.1 Key Registry (`registry/keys.toml`)

**Hard rules**
- Keys are append-only. No deletion.
- Each key has a stable, non-reusable numeric `key_id`.
- `Key.id` exists and is special: identity key (type `int`, `nullable=false`).
- Plans/Fragments **must not write** `Key.id`.

#### 3.1.1 Lifecycle, deprecation, and renames (governance)

Keys support a simple lifecycle to enable safe evolution without deletion.

**Defaults**
- Keys may declare a typed `default` value in the registry.
- For `nullable=false` keys (except `Key.id`), `default` is REQUIRED.

**Lifecycle fields (TOML)**
- `status`: `"active" | "deprecated" | "blocked"`
- `deprecated_since`: `"YYYY-MM-DD"` (optional)
- `replaced_by`: `<key_id>` (optional; used for renames)
- `allow_read`: `true|false` (default `true`)
- `allow_write`: `true|false` (defaults: `active=true`, `deprecated=false`, `blocked=false`)
- `sunset_after`: `"YYYY-MM-DD"` (optional; after this date, treat as `blocked`)

**Required rename rule**
- Renames are implemented as **new key** + deprecate the old key:
  - Create a new key with a new `key_id`.
  - Mark the old key as `status="deprecated"`, set `replaced_by=<new_key_id>`, and set `allow_write=false`.

**Engine/compiler enforcement**
- Writing a key with `allow_write=false` is **fail-closed** in production.
- Reading a key with `status="blocked"` is **fail-closed**.
- Reading a `deprecated` key is allowed but MUST emit a structured warning (and metrics) to track migration progress.

- For `nullable=false` keys, a registry `default` value is REQUIRED (typed), except for `Key.id` which must be present on all rows by construction.

**Schema (TOML)**
```toml
[[key]]
key_id = 1
name = "id"
type = "int"          # int|float|string|bool|feature_bundle
nullable = false
doc = "Entity identity (external system id)."

[[key]]
key_id = 1001
name = "model_score_1"
type = "float"
nullable = false
default = 0.0
doc = "ESR model score 1."
```

**Generated outputs**
- `dsl/packages/generated/keys.ts` exports `Key` tokens and types.
- `engine/include/keys.h` exports `enum class KeyId : uint32_t` and metadata table.
- `artifacts/keys.json` mapping name ↔ id for tooling.

### 3.2 Param Registry (`registry/params.toml`)

Rules mirror key registry:
- append-only, stable `param_id`, no reuse.
- types: `int|float|string|bool` + `nullable` + doc.

Generated outputs:
- `dsl/packages/generated/params.ts` exports `P` tokens.
- `engine/include/params.h` exports `enum class ParamId`.

#### 3.2.1 Lifecycle, deprecation, and renames

Params follow the same lifecycle model as keys:

- `status`: `"active" | "deprecated" | "blocked"`
- `replaced_by`: `<param_id>` for renames (new id + deprecate old)
- `allow_write`: controls whether request/plan/fragment may bind/override this param

**Enforcement**
- Using a `blocked` param is fail-closed.
- Overriding/binding a `deprecated` param should be fail-closed in prod mode by default (dev/test may allow with warnings).


---

### 3.3 Feature Registry (`registry/features.toml`)

Modern ranking systems may fetch **thousands of features** per stage. Modeling each feature as a top-level Key is often impractical. We introduce a **Feature Registry** plus a small number of **FeatureBundle Keys** (e.g., `Key.features_esr`) that carry large feature sets efficiently.

**Hard rules**
- Features are append-only. No deletion.
- Each feature has a stable, non-reusable numeric `feature_id`.
- Types: `int|float|string|bool` (future: `float_vec`).
- Features have lifecycle fields (`active|deprecated|blocked`) mirroring keys/params.
- Feature lifecycle changes MUST invalidate binary cache via `feature_registry_digest`.

**Schema (TOML)**
```toml
[[feature]]
feature_id = 20001
stage = "esr"
name = "country"
type = "string"
nullable = true
status = "active"
doc = "Viewer country."

[[feature]]
feature_id = 20002
stage = "esr"
name = "media_age_hours"
type = "float"
nullable = true
status = "active"
doc = "Age of media in hours."
```

**Generated outputs**
- `dsl/packages/generated/features.ts` exports `Feat` tokens grouped by stage (e.g., `Feat.esr.country`) and metadata.
- `engine/include/features.h` exports `enum class FeatureId` and metadata table.
- `artifacts/features.json` mapping name ↔ id for tooling.

**FeatureBundle Keys**
- Keys TOML should declare a small number of bundle keys:
  - `Key.features_esr` (type `feature_bundle`)
  - `Key.features_lsr` (type `feature_bundle`)
- `fetch_features(stage="esr")` writes the corresponding FeatureBundle key, not thousands of individual Keys.
- Only a small subset of features intended for downstream business logic should be materialized into top-level Keys via `extract_features(...)`.



## 4. Authoring surface (TypeScript)

### 4.1 Files
- Plan: `*.plan.ts` (authoring) → compiled to `artifacts/plans/*.plan.json`
- Fragment: `*.fragment.ts` → compiled to `artifacts/fragments/<name>/<version>.fragment.json`

### 4.2 No string-based access (except debug)
- Standard:
  - `row.get(Key.some_key)`
  - `row.set(Key.some_key, value)` returns a new RowSet/RowView semantics (pure functional).
- Debug-only (hidden under `_debug`):
  - `row._debug.get("some_key")`

### 4.3 CandidateSet / RowSet typing (static key-flow)

### 4.4 TypeScript typing policy (MVP)

The TS surface should provide **high-value guardrails** without requiring extreme type-level programming.

**Hard rule: no `any`**
- Do not use `any` in the codebase (including compiler and host bindings).
- Treat all untyped inputs as `unknown` and validate/parse into typed structures.

**Boundary typing pattern**
- `JSON.parse`, TOML parsing, HTTP request bodies, and QuickJS host values MUST enter the system as `unknown`.
- Convert `unknown` → typed IR only via explicit validators / type guards (fail-closed).

**Lint + TS config (MVP)**
- `tsconfig.json`: `"strict": true`, `"noImplicitAny": true`
- ESLint: `@typescript-eslint/no-explicit-any: "error"`

**Exception policy (rare)**
If a third-party type definition is broken and blocks progress:
- allow a **single-line** `eslint-disable` with an adjacent comment explaining why, and immediately convert the value to `unknown` and validate it.

## 5. Fragments (versioned reusable subgraphs)

### 5.1 Fragment API (TS)
Fragments are defined via:

```ts
export const esr = defineFragment({
  name: "esr",
  kind: "transform", // or "source"
  versions: {
    v0: (c, ctx) => c.fetch_features({stage:"esr"}).call_models({stage:"esr"}),
    v1: (c, ctx) => c.fetch_features({stage:"esr"}).call_models({stage:"esr_new"}),
  },
  default: "v0",
  limits: { maxNodes: 3000, maxDepth: 300 }
});
```

**Rules**
- Fragments may call other fragments.
- Fragment dependency graph must be acyclic (compile/link fails on cycles).
- Complexity limits enforced **per fragment version** at compile time:
  - `maxNodes`, `maxEdges`, `maxDepth`, `maxFanout` (exact list in §12).

### 5.2 Version selection at request time

### 5.3 Fragment arguments (macro parameters)

In addition to `version`, fragments may accept **structural arguments** (`args`) that affect graph structure or task parameters **at compile/link time**.

This is distinct from runtime-tunable parameters (`P.*`), which are resolved via the param system at execution time.

#### 5.3.1 Definition

Fragments may declare an `args_schema` with defaults:

```ts
export const esr = defineFragment({
  name: "esr",
  kind: "transform",
  args_schema: {
    stage: { type: "string", default: "esr" },
    topk:  { type: "int", default: 100 }
  },
  versions: {
    v0: (c, ctx, args) =>
      c.fetch_features({ stage: args.stage, trace: "esr_features" })
       .call_models({ stage: args.stage, trace: "esr_model" })
       .take({ count: args.topk, trace: "esr_take" })
  },
  default: "v0"
});
```

Rules:
- `args` must be JSON-serializable and must not contain `undefined` (compiler rejects).
- `args_schema` is validated at compile/link time (fail-closed).
- If `args` impacts `writes` (e.g., `stage`), the compiler/linker must compute `resolved_writes` for the node and the engine must double-check against task specs.

#### 5.3.2 Invocation

```ts
ctx.fragments.sourcing()
  .esr({ version: ctx.V("esr"), args: { stage: "esr", topk: 200 } });
```

#### 5.3.3 Optional request overrides (not required for MVP)

MVP may omit request-level overrides for fragment args. If implemented later:

```json
{
  "fragment_versions": { "esr": "v1" },
  "fragment_args": { "esr": { "topk": 200 } }
}
```

Resolution priority:
`request.fragment_args > plan/default args_schema > fragment args_schema`.

Plans can include version references resolved from request:

```ts
const c = ctx.fragments.sourcing()
  .esr({ version: ctx.V("esr") })
  .lsr({ version: ctx.V("lsr") });
```

Request includes:
```json
{
  "plan": "reels_plan_a",
  "fragment_versions": { "esr": "v1", "lsr": "v0" }
}
```

---

## 6. Parameters (bind/resolve) with priority: request > plan > fragment

### 6.1 Declaration (no immediate IO)
Plans/fragments declare param bindings:

```ts
ctx.param.bind(P.media_age_penalty_weight, {
  source: { kind: "redis", endpoint: "param_redis_server", key: "media_age_penalty_weight" },
  default: 0.2,
  cache_ttl_ms: 5000
});
```

This is a **declaration** recorded in IR. No runtime fetch happens during compilation.

### 6.2 Resolution algorithm (engine side)
On each request:

1. **Link** plan + selected fragment versions to build a linked DAG.
2. **Collect** `param_bindings` from all included artifacts.
3. For each `param_id`, select binding by priority:
   - request-level binding override (optional, if provided)
   - plan-level binding
   - fragment-level binding
4. If within the same priority level multiple bindings disagree → **fail-closed**.
5. Apply request-level **value overrides**:
   - `param_overrides[param_name] = value` (highest priority; bypasses IO)
6. Group remaining params by `source_id` and perform **one batch fetch per source**.
   - MVP: mock IO; real implementation can use Redis MGET / HTTP batch.
7. Fill `ParamTable` and enforce types:
   - `int`: finite + safe integer
   - `float`: finite number
   - `nullable=false`: value must not be null
   - missing: use `default` else null (if nullable) else fail

### 6.3 TTL meaning
`cache_ttl_ms` is an optional **server-side cache TTL** for cross-request caching of resolved param values.

---

## 7. Expressions and predicates (TS AST → IR)

### 7.1 `vm(outKey, expr, opts)`
Example:
```ts
c.vm(Key.final_score,
     Key.model_score_1 + Key.model_score_2 * 3 - Key.media_age * P.media_age_penalty_weight,
     { trace: "vm_final" });
```

**Compiler behavior**
- Parse TS AST.
- Extract `expr` AST and compile to **ExprIR** (no division in MVP).
- Rewrite source: `vm(outKey, __expr(expr_id), opts)`
- Store `expr_table[expr_id]` in the compiled artifact.

**ExprIR node types (MVP)**
- `const_number`, `const_bool`, `const_string`, `const_null`
- `key_ref(key_id)`
- `param_ref(param_id)`
- `add`, `sub`, `mul`, `neg`
- (optional) `coalesce(a,b)` (nice-to-have)

### 7.2 `filter(predicate, opts)`
Examples:
```ts
c.filter(Key.country.in(["US","CA"]) && Key.esr_score > P.esr_cutoff, { trace: "policy" });
c.filter(Key.title.regex(P.blocklist_regex, { flags: "i" }), { trace: "title_blocklist" });
```

**Compiler behavior**
- Parse predicate AST into **PredIR**.
- Rewrite source: `filter(__pred(pred_id), opts)`
- Store `pred_table[pred_id]`.

**PredIR node types (MVP)**
- boolean ops: `and`, `or`, `not`
- comparisons: `== != < <= > >=`
- `in(lhs, [literal...])` (literals only in MVP)
- `regex(lhs, pattern_ref, flags)` where `pattern_ref` can be literal or `param_ref`
- `is_null(x)`, `not_null(x)` (recommended)

**Null semantics**
- Only `== null` / `!= null` (or `is_null`) have explicit null semantics.
- Other comparisons with null evaluate to `false`.

**Regex engine**
- Use **RE2** in C++ engine for safe, predictable runtime.
- Optimization (MUST): for dict-encoded strings, run regex once on dictionary values → produce `matching_codes` bitset.

---

## 8. Task catalog (MVP)

All tasks accept `trace?: string` param (optional), used in audit logs and tracing spans.

### 8.1 Source / Sourcing
- `viewer.follow({fanout, trace?}) -> CandidateSet`
- `CandidateSet.media({fanout, trace?}) -> CandidateSet`
- `viewer.fetch_cached_recommendation({fanout, trace?}) -> CandidateSet`

(For MVP: these can be mock sources returning synthetic candidates with `Key.id`. Production: source tasks call external systems such as Redis/DB/streaming/services to produce initial candidate ids.)

### 8.2 Composition
- `concat(a, b) -> CandidateSet`
  - Output schema is union of columns.
  - Missing columns on either side are materialized as nulls.
  - String dict columns must be unified (see §10).

### 8.3 Feature + Model
- `fetch_features({stage, trace?}) -> CandidateSet`
  - Writes the stage FeatureBundle key (e.g., `Key.features_esr`).
  - Optionally declares `writes_features` (feature_id set) for audit.
- `call_models({stage, trace?}) -> CandidateSet`
  - Adds model score keys depending on stage.
  - Does not sort.

### 8.4 Transform
- `vm(outKey, expr, {trace?}) -> CandidateSet`
  - Writes exactly `outKey`.
- `filter(pred, {trace?}) -> CandidateSet`
  - Does not write keys; updates selection.
- `extract_features({ from, map, trace? }) -> CandidateSet`
  - `from` is a FeatureBundle key (e.g., `Key.features_esr`).
  - `map` is a list of `{ feature: FeatToken, out: KeyToken }`.
  - Reads the specified features from the bundle and materializes them as top-level Keys.
  - Enforces type/nullability against both Feature Registry and Key Registry (fail-closed).
- `dedupe({by=Key.id, strategy="first"|"last"|"max_by", scoreKey?, trace?}) -> CandidateSet`
  - Default stable first.
- `sort({by, order="asc"|"desc", trace?}) -> CandidateSet`
  - Produces/updates permutation.
- `take({count, trace?}) -> CandidateSet`
  - Keeps top-K according to current order (or current iteration order if unsorted).

### 8.5 Join (SQL-like)
`lhs.join(rhs, { how, by=Key.id, select?, map?, trace? }) -> CandidateSet`

Additional join options:
- `on_missing`: how to populate rhs-derived output keys when no rhs match exists (only for `how="left"`).
  - `"null"` (default): write `null` for missing matches.
  - `"default"`: write the Key registry `default` value for missing matches (requires `default` to be present).


Supported `how` (MVP):
- `inner`, `left`, `semi`, `anti`

Rules:
- Default `by=Key.id`.
- **No overwrite**: output keys must not already exist in `lhs`. Conflict → fail-closed.
- For `how="left"`:
  - If `on_missing="null"` (default), all rhs-derived output keys MUST be `nullable=true`.
  - If `on_missing="default"`, rhs-derived output keys MUST either be `nullable=true` or declare a registry `default` (and the engine uses the default on missing matches).
- `select` copies rhs keys with same names (only if no conflict).
- `map` copies rhs keys into *new* output keys (must be pre-declared).

Semantics:
- `left`: keeps all lhs rows; missing matches set rhs outputs to null.
- `inner`: keeps only matched lhs rows (lhs order stable).
- `semi`: keeps only matched lhs rows; does not add rhs keys.
- `anti`: keeps only unmatched lhs rows; does not add rhs keys.

RHS duplicate ids:
- If rhs contains duplicate `by` in active rows → fail-closed (require explicit `dedupe` upstream).

---

## 9. C++23 Engine

### 9.1 Two components
1. **DAG Interpreter / Executor**
2. **HTTP Server wrapper** (MVP)

### 9.2 Runtime data model

#### ColumnBatch (SoA)
- Each key maps to a typed column:
  - `IntColumn (int64) + validity bitset`
  - `FloatColumn (float64 or float32; MVP choose float64 for simplicity)`
  - `BoolColumn + validity`
  - `StringDictColumn { dict: vector<string>, codes: vector<int32>, validity }`
  - `FeatureBundleColumn` (typed container holding many feature columns keyed by `FeatureId`)

#### RowSet
A pipeline value is:
- `base_batch: shared_ptr<ColumnBatch>`
- `selection: optional<SelectionVector>` (active rows)
- `order: optional<PermutationVector>` (current logical order)

Rules:
- `filter` updates selection.
- `sort` updates order (permutation), should not eagerly permute columns.
- `take` truncates logical iteration (selection/order), may materialize only at output.

### 9.3 Dictionary unification (concat) — deterministic algorithm (required)
When concatenating two batches for a string key:
- If dictionaries are identical (pointer or identical content id) → fast path append codes.
- Otherwise:
  - Build merged dictionary deterministically:
    - Start with left dict values in order, then append right dict values not already present.
  - Build remap for left/right codes → merged codes.
  - Produce output codes by remapping and concatenating.

**Optimization (recommended)**:
- Maintain a per-request `StringInterner` per key_id so most tasks output canonical dictionaries, making concat fast-path common.

### 9.4 Task interface (C++)
Each task implementation provides:

- `TaskSpec spec()` including:
  - `op` string
  - `params_schema`
  - `default_budget` (timeout/cpu/mem)
  - `reads` (static) and `writes_fn(params)` (param-dependent writes)

- `run(inputs: vector<RowSet>, params, ExecCtx) -> RowSet`
  - May be synchronous or asynchronous (folly Future / coroutines).
  - Must enforce:
    - key writes only to allowed outputs (spec + registry)
    - never write `Key.id`
    - type enforcement on writes (`int` safe integer etc.)
    - budgets (timeout/cpu/mem) via ExecCtx

### 9.5 Engine compile/link/validate (fail-closed)
Before executing a request:
- Load `plan.json` and referenced fragment artifacts.
- Resolve fragment versions from request (with defaults).
- Link fragment calls into a linked DAG.
- Validate:
  - DAG is acyclic, node ids unique
  - task `op` exists in registry
  - param schemas validate node params
  - `resolved_reads/writes` are consistent with task spec for given params
  - keys exist in key registry
  - no node writes `Key.id`
  - complexity budgets are within limits

---

### 9.6 Binary cache for linked plans (recommended)

JSON artifacts (`plan.json`, `fragment.json`) are the source of truth for review and debugging. To reduce cold-start and per-request overhead, the engine should support an internal **binary cache** of the **linked + validated** plan.

**Flow**
1. Load JSON artifacts → validate → link (resolve fragment versions/args) → produce a linked DAG.
2. Serialize the linked DAG into a binary blob (format implementation-defined; e.g., protobuf/flatbuffers/capnp).
3. Cache by a deterministic key; reuse the cached binary form for subsequent requests.

**Cache key (MUST include)**
- `schema_version`
- `plan_digest`
- selected fragment digests (by name+version)
- `key_registry_digest`, `param_registry_digest`, `feature_registry_digest`, `task_manifest_digest`
- `fragment_versions` and any structure-affecting `fragment_args` digest

**Correctness**
- If any digest changes, the cached entry must be ignored and rebuilt.
- Runtime execution must still enforce budgets and lifecycle rules (deprecated/blocked keys/params/features).



## 10. Execution performance options (MVP + extensibility)

MVP must implement:
- Topological scheduling with ready queue
- Async tasks supported (folly Futures preferred; MVP may stub with synchronous mocks)
- Concurrency limits per IO category (params/features/models) via semaphores
- SelectionVector to avoid materialization
- Regex-on-dictionary optimization

Production hooks (documented, not required for MVP):
- Critical-path scheduling
- Cross-request caches (params/features)
- Chunked (vector-at-a-time) execution
- Deadline propagation and cancellation

---

## 11. Audit logs (JSON Lines)

Engine emits three log streams (or one stream with `log_type`):

### 11.1 Request-level
Fields:
- `request_id`, `plan`, `fragment_versions`, `status`, `latency_ms`
- optional digests: `linked_plan_digest`, `param_plan_digest`

### 11.2 Task-level
Fields:
- `request_id`, `node_id`, `op`, `plan`
- `origin.fragment`, `origin.version`, `origin.path`
- `num_inputs` (or `num_inputs_left/right`), `num_outputs`
- `latency_ms`
- `trace` (optional)

### 11.3 Param-level
Fields:
- `request_id`, `param_name`, `value`
- `origin` (`request_value|plan|fragment|source|default|null`)
- `source` (if IO-based)

---



## 11.4 Debuggability: mapping runtime errors back to `*.ts` sources (SourceRef)

Because this is a compiled DSL (TS → JSON IR → engine), runtime errors must be reportable with precise source locations.

### 11.4.1 SourceRef tables in IR

Compiled artifacts MUST include:
- `source_files`: stable ids → `{ path, digest }`
- `source_spans`: stable span ids → `{ file, start(line,col), end(line,col) }`

### 11.4.2 Node-level spans

Each DAG node MUST include:
- `source_span`: span id pointing to the TS callsite that created the node
- optional `callsite_span`: for nodes originating from fragment expansion, point to the parent plan's fragment invocation location

### 11.4.3 ExprIR/PredIR spans (sub-expression mapping)

For `vm` and `filter`, the compiler MUST assign a `sid` (span id) to each IR node produced from TS AST.

At runtime, evaluation errors (e.g., integer overflow, invalid cast, NaN/Inf write) MUST surface:
- `node_id`, `op`, `trace`
- `expr_id/pred_id` (if relevant)
- the failing `sid` so the engine can print an exact TS range (line/col)

### 11.4.4 Error payload shape (recommended)

Engine should emit structured errors with:
- `request_id`, `plan`, `node_id`, `op`, `origin.fragment/version/path`
- `source_span` resolved to `path:line:col-range`
- optional `expr_id/pred_id` and `sid` resolved to a narrower `path:line:col-range`



## 11.5 Critical path identification (debuggability requirement)

For large DAGs, developers cannot reliably infer execution order from authoring order. The system MUST support identifying and visualizing a request's **critical path** from trace data.

### 11.5.1 Required per-node trace timestamps

When a request is traced (sampled or triggered), each node record MUST include:
- `preds[]`: predecessor node ids (dependencies)
- `ready_ts`: timestamp when all deps are satisfied and the node enters the ready queue
- `start_ts`: timestamp when the node actually begins execution (after acquiring any semaphores/executor slots)
- `end_ts`: timestamp when the node completes

The engine SHOULD also record:
- `semaphore_acquired_ts` (or equivalent) to break down queueing vs execution latency.

### 11.5.2 Critical path definitions

Visualizer/tools MUST support two critical-path modes:
1) **Latency critical path** (recommended default): node cost = `end_ts - ready_ts` (includes scheduling/queue waits + execution).
2) **Service-time critical path**: node cost = `end_ts - start_ts` (execution only).

### 11.5.3 Computation (deterministic)

Given an acyclic DAG and per-node costs, tools compute the critical path by longest-path DP over a canonical topological order:
- `dp[v] = cost[v] + max(dp[p] for p in preds[v])` (or `cost[v]` if no preds)
- maintain `parent[v] = argmax(...)`
- choose sink among plan outputs (or request end) by maximum `dp`
- recover path by following `parent` pointers

### 11.5.4 Visualization requirements

The plan/trace visualizer SHOULD:
- highlight the critical path nodes/edges
- show per-node breakdown:
  - `wait = start_ts - ready_ts`
  - `run = end_ts - start_ts`
  - `cost = end_ts - ready_ts`
- support collapsing by fragment/version (supernodes) and expanding along the critical path for very large graphs

This feature is essential for explaining regressions where adding an async node increases end-to-end latency without obvious per-node runtime changes.

## 12. Complexity budgets (compile-time checks)

Enforced at compile time for each compiled artifact:
- `max_nodes`
- `max_edges`
- `max_depth`
- `max_fanout` / `max_fanin`
- optional: `max_fragments_called`

If exceeded → compilation fails with a human-readable report pointing to fragment/plan.

---

## 13. HTTP Server (MVP)

### 13.1 Endpoint
`POST /rank`

Request JSON:
```json
{
  "request_id": "uuid-or-string",
  "plan": "reels_plan_a",
  "fragment_versions": { "esr": "v1", "lsr": "v0" },
  "param_overrides": { "media_age_penalty_weight": 0.35 },
  "output_keys": ["id", "final_score", "country"]
}
```

Rules:
- `param_overrides` has highest priority (over plan/fragment bindings).
- Missing `fragment_versions` entries use fragment defaults.

Response JSON:
```json
{
  "request_id": "…",
  "candidates": [
    { "id": 123, "fields": { "final_score": 0.91, "country": "US" } },
    { "id": 456, "fields": { "final_score": 0.87, "country": "CA" } }
  ]
}
```

MVP returns:
- `id` always
- plus requested output fields (default to all fields in MVP if unspecified; production should default to a safe minimal set)

---

## 14. Tooling (dslc compiler)

`dslc` responsibilities:
1. Parse TS files.
2. Extract and compile `vm` expressions → ExprIR table.
3. Extract and compile `filter` predicates → PredIR table.
4. Rewrite TS source to use `__expr(id)` / `__pred(id)`.
5. Compile TS → JS (esbuild/tsc) into restricted subset.
6. Execute JS in **QuickJS** with a host-injected DSL runtime that builds a DAG.
7. Output artifact JSON:
   - plan: `*.plan.json`
   - fragment version: `<name>/<version>.fragment.json`

QuickJS restrictions:
- No `eval`, no `Function`, no dynamic imports.
- No filesystem/network by default.
- Fragment-level “IO” for MVP is mocked or expressed as explicit IO Tasks (capability-gated).

---



### 14.1 SourceRef generation (MUST)

During compilation, `dslc` MUST:
- Record `source_files` entries for each referenced TS file (plan + fragments).
- Generate `source_spans` for:
  - each task invocation (node-level `source_span`)
  - each ExprIR/PredIR IR node (`sid`) derived from TS AST
- Persist these tables into the output JSON artifacts.

### 14.2 Compiler-side governance checks (DX requirement)

To provide consistent developer experience, `dslc` MUST load the registries (`keys.toml`, `params.toml`, `features.toml`) during compilation and perform the same fail-closed validations as the engine where possible, including:
- blocked/deprecated/allow_write checks for keys/params/features
- join `how/on_missing` nullable/default constraints
- fragment args schema validation
- complexity budgets per artifact

The engine still repeats these checks at load/link time (belt-and-suspenders).



## 15. MVP scope and non-goals

### Must-have (MVP)
- Key/Param registry parsing + TS/C++ codegen
- DSL runtime types for key-flow checks (basic)
- AST extraction → ExprIR/PredIR + rewrite
- QuickJS graph build
- Artifact JSON schemas + link/compile/validate
- Engine executor with:
  - columnar batches + dict strings
  - selection vector + permutation
  - tasks: concat/fetch_features/call_models/vm/filter/dedupe/sort/take/join
  - budgets (basic timeouts) + fail-closed
  - request/task/param logs
- HTTP server wrapper

### Non-goals (MVP)
- Real feature stores / model inference (use mocks)
- Distributed execution
- Advanced optimizer (predicate pushdown, etc.)
- FULL SQL join types (right/full) and multi-key joins
- Rich regex flags beyond `i`

---

## 16. Acceptance tests (minimum)

1. **Registry codegen**
   - keys/params TOML → generated TS/C++ files deterministic
2. **No undefined**
   - Plan compilation fails if an `undefined` would flow into a DSL API
3. **ExprIR**
   - `vm` expressions compile to IR and evaluate correctly in engine
4. **PredIR**
   - `filter` with `in` + `regex(P.param)` behaves correctly; regex optimized via dictionary scan
5. **Fragments**
   - version selection via request works; cycles rejected
6. **Join**
   - left/inner/semi/anti semantics correct; key overwrite rejected
7. **Performance sanity**
   - selection vector avoids materialization (verify by instrumentation)
8. **Logs**
   - request/task/param logs emitted with required fields

---

## 17. Example (end-to-end)

- `sourcing.fragment.ts` (v0) creates connected + unconnected and concatenates
- `esr.fragment.ts` has v0/v1 model versions
- `reels_plan_a.plan.ts` composes `sourcing -> esr(v=ctx.V("esr")) -> vm -> sort -> take(10)`
- Run server, POST `/rank` with `fragment_versions.esr=v1`, verify outputs and logs


---

## 18. Pilot packaging for user testing (recommended)

This project includes a lightweight **developer pilot** to evaluate DSL usability and “bug left-shift” claims. To minimize setup friction (especially for participants outside an internal company environment), the recommended distribution model is:

### 18.1 Best practice: prebuilt engine binary + one-command pilot script (no Docker)

**Motivation**
- Pilot participants should focus on **authoring/debugging experience**, not environment setup.
- Bundling a full macOS C++ toolchain is impractical (macOS SDK/sysroot, codesigning, large downloads).
- Requiring Xcode Command Line Tools increases drop-off and introduces non-deterministic setup failures.
- A prebuilt engine binary enables a near-zero-install experience while keeping the repository reproducible.

**Approach**
- Provide a **prebuilt** `engine` server binary for macOS (preferably `universal2`, or separate `arm64`/`x86_64` builds).
- Provide a single entrypoint script: `bench/pilot/run.sh` that:
  1) checks prerequisites (Node.js LTS)
  2) installs JS dependencies (`npm ci` / `pnpm i --frozen-lockfile`)
  3) compiles plans/fragments via `dslc`
  4) starts the server using the prebuilt `engine`
  5) executes a sample request to verify the environment
- The pilot package must use only **mock** external IO (features/models/params), unless explicitly running in a controlled internal environment.

**Recommended layout**
```
bench/pilot/
  README.md
  run.sh
  env_check.sh
  bug_pack/
  sample_requests/
  participant_template.txt
bin/
  engine_darwin_universal2          # or engine_darwin_arm64 / engine_darwin_x86_64
```

### 18.2 Optional alternatives
- **Remote SSH sandbox** (team-internal): provide an AWS/remote VM with all dependencies pre-installed.
- **Local build** (advanced): allow `xcode-select --install` + CMake build, but this should not be the default for pilots.


