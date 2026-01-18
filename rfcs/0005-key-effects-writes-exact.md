---
rfc: 0005
title: "Key Effects and writes_exact: Core Effect Inference + Strict Shape Enforcement"
status: Implemented  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-14
updated: 2026-01-18
authors:
  - "<name>"
approvers:
  - "<name>"
requires: [0001]
replaces: []
capability_id: "cap.rfc.0005.key_effects_writes_exact.v1"
---

# RFC 0005: Key Effects and writes_exact - Core Effect Inference + Strict Shape Enforcement

## 0. Summary
RFC 0005 covers two related but distinct concerns and explicitly separates them:

### A) Core language metadata (no capability needed)
All plans benefit from compiler-produced key effect metadata:
- A `key_effects` field in the Plan/Fragment artifact (analogous to `expr_table`, `pred_table`, `inputs`).
- A small, deterministic effect language in TaskSpec to describe how a task's written keys depend on link-time constants.
- A three-valued effect result: `Exact(K) | May(K) | Unknown`.
- A normative evaluation procedure + subgraph aggregation rules.

This part does not add new runtime behavior. It describes what the plan already does and makes it explicit and testable.

### B) Capability-gated strict enforcement (keeps `cap.rfc.0005...`)
Some features (notably meta-tasks with branching/fallback) require strict validation:
- Branches must preserve RowSet shape (v1 constraints)
- Branch outputs must be provably exact and shape-equal:
  - `writes_exact(true) == writes_exact(false)` (for `if_request`)
  - `writes_exact(success) == writes_exact(fail)` (for `timeout_wrapper`)
- If `Exact` is not provable, the compiler/engine must fail-closed in these contexts.

Strict enforcement is gated by capability `cap.rfc.0005.key_effects_writes_exact.v1` (Scheme B, RFC 0001).

---

## 0.1 Why the split exists
Effect inference and capability extensions are different kinds of "features":

- Effect inference: compile-time metadata describing what a plan already does (universal value; no opt-in needed).
- Strict enforcement: additional validation requirements that change what artifacts are accepted in certain contexts (selective; opt-in via capability).

This RFC treats effect inference as core metadata and capability gates only the strict enforcement rules.

## 1. Motivation
Several meta-features require deterministic, reviewable guarantees about RowSet schema:

- `timeout_wrapper` requires success/fallback branches to write the same keys deterministically.
- `if_request` requires both branches to preserve RowSet shape and write the same keys deterministically.
- Debug tooling and plan review benefit from compiler-produced "schema lens" information at each node.

Without a formal definition, "writes_exact" becomes an informal idea with inconsistent interpretations, leading to:
- divergent implementations across languages (TS/C++/Rust),
- silent behavior changes during refactors,
- weak or non-existent fail-closed validation for branching meta-tasks.

This RFC makes key effect inference a first-class, testable contract, and gates only the strict enforcement behavior.

## 2. Goals

- G0 (Core): Provide a stable, deterministic `key_effects` metadata output that all plans can use.
- G1: Define `writes_exact` formally and make it decidable under a restricted effect language.
- G2: Enable fail-closed validation when a feature requires deterministic schema (branch shape equality).
- G3: Support tasks whose written keys depend on a small set of enumerated parameters (e.g., `stage`), without allowing arbitrary dynamic keys.
- G4: Provide a uniform way to compute:
  - `writes_effect(node, Γ)` in `{Exact, May, Unknown}`,
  - `writes_exact(node)` when provable,
  - `writes_may(node)` as a safe finite over-approximation when available.
- G5: Keep the effect system small enough to implement consistently in dslc and the engine.

## 3. Non-Goals (v1)

- NG1: Proving semantic properties of values (monotonicity, invariants) - only schema/key effects.
- NG2: Per-row (data-dependent) effects or branching - effects may depend only on link-time constants and bounded enums.
- NG3: Inferring effects from arbitrary user code - effects are declared in TaskSpec; inference only evaluates them.
- NG4: Full dependent typing over values - this is an effect system / refinement over RowSet schema.

## 4. Definitions

### 4.1 Key, RowSet schema
- A Key is a registry-governed top-level field identified by stable `key_id`.
- A RowSet schema at a point in the graph can be modeled as the set of keys that are present (with types/nullability from the registry).

### 4.2 Compile/link-time environment Γ
Γ contains information known at compile/link time:
- literal constants in authoring code,
- fragment args after linking (resolved constants),
- plan-level constants (if supported),
- build-time constants such as `ctx.env` (if supported).

Γ does not include request-time values.

### 4.3 Effect results
Effect evaluation produces one of:

- `Exact(K)`: The task/subgraph will write exactly the finite set of keys `K`.
- `May(K)`: The task/subgraph may write some subset of `K`; `K` is a finite over-approximation.
- `Unknown`: A safe finite over-approximation is not available (or forbidden by policy).

Define helper projections:
- `keys(Exact(K)) = K`
- `keys(May(K))   = K`
- `keys(Unknown)  = ∅` (only for diagnostics; not a valid over-approx)

All sets `K` in artifacts MUST be sorted + unique (by `key_id` lexicographic order).

### 4.4 writes_exact
`writes_exact(node)` is defined iff `writes_effect(node, Γ) = Exact(K)`.
In that case, `writes_exact(node) = K`.

Similarly:
- `writes_may(node) = K` if `writes_effect(node, Γ) ∈ {Exact(K), May(K)}`
- `writes_may(node)` is undefined for `Unknown`

### 4.5 Engine-evaluated writes_eval (core)

The engine evaluates key effects **at plan-load time** during `validate_plan()`. Effects are stored on the parsed Node struct and exposed via `--print-plan-info`.

Node struct fields (C++):
```cpp
struct Node {
  // ... existing fields ...
  EffectKind writes_eval_kind = EffectKind::Unknown;  // Exact | May | Unknown
  std::vector<uint32_t> writes_eval_keys;             // sorted, deduped; empty for Unknown
};
```

CLI output shape (`--print-plan-info`, normative):
```json
{
  "nodes": [
    {
      "node_id": "n12",
      "op": "fetch_features",
      "writes_eval": {
        "kind": "Exact",
        "keys": [2001, 4001]
      }
    }
  ]
}
```

Rules:
- `writes_eval.kind` is one of: `"Exact"`, `"May"`, `"Unknown"` (PascalCase)
- `writes_eval.keys` MUST be sorted + unique (by key_id ascending)
- When `kind = "Unknown"`, `keys` is empty `[]`
- Engine MUST populate `writes_eval` for all nodes during validation

## 5. TaskSpec: declarative key effect language

### 5.1 Requirement: no dynamic keys
TaskSpec MUST NOT allow tasks to create keys dynamically from strings or data.
All written keys must be expressible as a finite set of registry `key_id`s.

### 5.2 Writes Contract: two-field design (v1)

TaskSpec declares writes using **two complementary fields**:

```cpp
struct TaskSpec {
  std::vector<KeyId> writes;                     // Fixed/static key writes
  std::optional<WritesEffectExpr> writes_effect; // Dynamic/param-dependent
};
```

The **Writes Contract** is computed as:
```
Writes Contract = UNION( Keys(writes), writes_effect )
```

**Key insight:** Most tasks only fill ONE field:
- Source tasks (fixed schema) → `.writes = {3001, 3002}`, omit `.writes_effect`
- Row-only ops (filter, take) → `.writes = {}`, omit `.writes_effect`
- Param-dependent ops (vm) → `.writes = {}`, `.writes_effect = EffectFromParam{"out_key"}`

### 5.3 Effect expression forms

The `.writes_effect` field uses one of the following forms:

1) **EffectKeys** - Static set (rarely used directly; prefer `.writes`)
```cpp
EffectKeys{{K1, K2, ...}}
```

2) **EffectFromParam** - Key from param value
```cpp
EffectFromParam{"out_key"}
```
- The param value must be a `key_id` at link time for `Exact`.

3) **EffectSwitchEnum** - Enum-dependent keys
```cpp
EffectSwitchEnum{"stage", {{"esr", makeEffectKeys({4001})}, {"lsr", makeEffectKeys({4002})}}}
```
- If `stage` is link-time constant → `Exact` of the selected case.
- If `stage` is not constant but domain is bounded → `May` of the union.
- If domain is unknown/unbounded → `Unknown`.

4) **EffectUnion** - Combines multiple sources
```cpp
EffectUnion{{makeEffectKeys({1001}), makeEffectFromParam("out")}}
```

### 5.4 compute_effective_writes() helper

The engine provides `compute_effective_writes(spec)` to compute the union:
- If only `.writes` → `EffectKeys{writes}`
- If only `.writes_effect` → `*writes_effect`
- If both → `EffectUnion{Keys(writes), *writes_effect}`
- If neither → `EffectKeys{}` (empty, no writes)

This restricted language is sufficient for common tasks:
- `vm`: `EffectFromParam{"out_key"}`
- stage-dependent bundle tasks via `EffectSwitchEnum`.

### 5.5 Optional extension: path-based key lists (not yet implemented)
For params that contain arrays/objects with embedded keys, TaskSpec may declare:
- `FromPath("items[].out")`

Semantics match `FromParam`, but the param value is a finite list of key_ids.
`Exact` requires the list to be fully known at link time; otherwise `Unknown` (or `May` if bounded and enumerated).

## 6. Effect evaluation (normative)

### 6.1 param_bindings (Γ)

The evaluation environment `param_bindings` (formerly called Γ) maps param names to their concrete values:

```cpp
using EffectGamma = std::map<std::string, std::variant<uint32_t, std::string>>;
```

It is built from `ValidatedParams` (which has defaults applied and types normalized):
- **Int params** → `uint32_t` (for `EffectFromParam`, e.g., `out_key`)
- **String params** → `std::string` (for `EffectSwitchEnum`, e.g., `stage`)

Example:
```cpp
// Node params: {"out_key": 2001, "trace": "vm_final"}
// param_bindings: {"out_key" → 2001}
```

### 6.2 Evaluation function

Define `eval_writes(effect_expr, param_bindings) -> WritesEffect{kind, keys}`.

Implementation in `engine/src/writes_effect.cpp`:

```cpp
WritesEffect eval_writes(const WritesEffectExpr& expr, const EffectGamma& gamma);
```

Rules (v1):

- `eval_writes(EffectKeys{K...}, _) = Exact({K...})` (sorted + deduped)

- `eval_writes(EffectFromParam{p}, gamma)`:
  - if gamma binds `p` to a `uint32_t` key_id: `Exact({key_id})`
  - else: `Unknown`

- `eval_writes(EffectSwitchEnum{param, cases}, gamma)`:
  - if gamma binds `param` to a `std::string v` and `v ∈ cases`: eval the selected case
  - else if all cases are Exact: `May(union over all case keys)`
  - else: `Unknown`

- `eval_writes(EffectUnion{items}, gamma)`:
  - evaluate each `ri = eval_writes(items[i], gamma)`
  - if any `ri.kind = Unknown`: result is `Unknown`
  - else if all are `Exact`: `Exact(union(keys))`
  - else: `May(union(keys))`

All result keys are sorted and deduplicated.

### 6.2 Policy: where `May` / `Unknown` are acceptable
The system must distinguish contexts:

- Lens/tooling contexts (schema preview, dependency graph, plan review):
  - `May` and `Unknown` are acceptable outputs of effect inference.

- Strong-shape contexts (branching/fallback meta-tasks):
  - `May` and `Unknown` are NOT acceptable.
  - These contexts are only allowed when strict enforcement is enabled (see §9).

## 7. Subgraph aggregation rules

### 7.1 Node-level effects
Each node's `writes_eval` is computed during `validate_plan()`:

1. `validate_params(node.op, node.params)` → `ValidatedParams` (with defaults + normalization)
2. `build_param_bindings(validated_params)` → `EffectGamma`
3. `compute_effective_writes(spec)` → `WritesEffectExpr` (union of `.writes` and `.writes_effect`)
4. `eval_writes(expr, param_bindings)` → `WritesEffect{kind, keys}`
5. Store in `node.writes_eval_kind` and `node.writes_eval_keys`

### 7.2 Subgraph writes
For a subgraph `S` containing nodes `{n1..nk}`, define:

- `writes_effect(S, Γ) = Union([writes_effect(n1, Γ), ..., writes_effect(nk, Γ)])`

So:
- `writes_exact(S)` exists iff no node is `Unknown` and no node is `May`.

### 7.3 Overwrite / conflict constraints
This RFC does not redefine overwrite rules. However:
- When aggregating `writes_exact`, implementations MUST still apply existing governance rules (e.g., "no overwrite" or "only explicit out keys").
- In strong-shape contexts, conflicting writes that would make output ambiguous MUST fail-closed.

## 8. Interaction with RowSet shape effects
TaskSpec already classifies rowset behavior (conceptually):
- `Preserve` (enrichment-only)
- `MutatesSelection` (filter)
- `MutatesPermutation` (sort)
- `MutatesRowCount` (join/concat/take etc., depending on semantics)

This RFC focuses on key writes, but notes:
- Meta-tasks such as `timeout_wrapper` and `if_request` additionally require `rowset_contract = preserve` in v1.
- A future RFC may formalize `rowset_effect_exact` and merging semantics.

## 9. Strict shape enforcement (capability-gated, normative)

### 9.1 Capability gate
Strict enforcement is gated by capability:

- `cap.rfc.0005.key_effects_writes_exact.v1`

Rules:
- If an artifact requires this capability, the engine MUST:
  - validate the capability per RFC 0001,
  - enforce the strict rules in §9.2-§9.4 fail-closed.
- If an artifact does NOT require this capability, the compiler/engine MUST NOT accept any construct that depends on strict enforcement
  (e.g., branching meta-tasks that require `writes_exact` equality). It MUST fail-closed rather than silently weakening validation.

Compiler behavior:
- dslc SHOULD automatically add `cap.rfc.0005.key_effects_writes_exact.v1` to `capabilities_required`
  whenever it lowers a plan that contains a strict-shape meta-task requiring it (e.g., `timeout_wrapper`, `if_request`).

### 9.2 timeout_wrapper (RFC 0002)
When strict enforcement is enabled, v1 requires:

- `rowset_contract = preserve` for both `if_success` and `if_fail` subgraphs
- `writes_exact(if_success) == writes_exact(if_fail)`

If either branch yields `May` or `Unknown`, validation MUST fail-closed with a diagnostic identifying the node/param causing non-exact effects.

### 9.3 if_request (RFC 0004)
When strict enforcement is enabled, v1 requires:

- `rowset_contract = preserve` for both branches
- `writes_exact(if_true) == writes_exact(if_false)`

Same failure posture as §9.2.

### 9.4 Fail-closed when `Exact` is not provable
In strict-shape contexts:
- If `writes_effect(...)` is `May` or `Unknown`, compilation/link/validation MUST fail-closed.
- Implementations MUST NOT treat `May` as `Exact`, even if the `May` set happens to match on both branches.

## 10. Diagnostics requirements (DX)
When `Exact` is required but not available, the compiler must produce:
- the specific node (op + node_id) and source span,
- which param caused non-exactness (e.g., `stage`),
- the computed `May{...}` set if applicable,
- and a suggested fix (e.g., "make stage a link-time constant" or "use fill_defaults to equalize outputs").

Example message (conceptual):
> Cannot prove writes_exact for node n12 (fetch_features): param `stage` is runtime-bound. In this context, deterministic shape is required.
> Computed writes_may = {Key.features_esr, Key.features_lsr}. Fix: make `stage` a link-time constant (literal/fragment arg), or move this task outside the wrapper.

## 11. Compatibility and rollout

- Core metadata:
  - `key_effects` is additive and optional. Once emitted, engines/parsers must allow it under strict schema validation.
  - Rollout approach (recommended):
    1) Engine accepts (parses/ignores) `key_effects` field.
    2) dslc begins emitting `key_effects` deterministically.
    3) Tooling consumes metadata.
- Strict enforcement:
  - Gated by capability `cap.rfc.0005...`.
  - Meta-tasks that rely on strict enforcement should cause dslc to auto-require the capability.

## 12. Test plan

### 12.1 Unit tests (dslc)
- `vm(out=Key.x)` yields `Exact({Key.x})`.
- `fetch_features(stage="esr")` yields `Exact({Key.features_esr})`.
- `fetch_features(stage=P.stage)` with bounded enum yields `May({Key.features_esr, Key.features_lsr})`.
- same case but unbounded domain yields `Unknown`.
- Union combination rules for Exact/May/Unknown.

### 12.2 Integration tests (strict enforcement)
With `cap.rfc.0005...` required:
- `timeout_wrapper` rejects branches with `May`/`Unknown` effects.
- `if_request` rejects branches with mismatched `writes_exact`.
- Diagnostics include node_id, param name, and computed may set.

Without `cap.rfc.0005...` required:
- Any plan containing constructs that depend on strict-shape enforcement must fail-closed.

### 12.3 Golden fixtures (cross-language)
- A JSON fixture describing a TaskSpec writes expression and a set of Γ bindings, with expected result.
- Run fixture tests in both dslc (TS) and engine (C++/Rust) implementations.

## 13. Alternatives considered
- Gate effect inference itself behind a capability.
  - Con: prevents universal tooling and makes "metadata presence" an opt-in, which is backwards from its purpose.
- Treat all param-dependent writes as Unknown and forbid them globally.
  - Too restrictive; common patterns like stage bundles become painful.
- Allow arbitrary dynamic keys.
  - Breaks governance and determinism.

## 14. Open questions
- OQ1: Formalize `reads_exact` / `reads_may` similarly (especially for ExprIR/PredIR references).
- OQ2: Extend effect language with bounded `FromPath` for common map/list patterns (extract_features).
- OQ3: Formalize rowset mutation effects and merge semantics for future control-flow features.

---

## 15. Implementation Status (Steps 12.1-12.3)

This section documents the actual implementation as of 2026-01-18.

### 15.1 Capability Registration (Step 12.1)

The capability is registered in `registry/capabilities.toml`:

```toml
[[capability]]
id = "cap.rfc.0005.key_effects_writes_exact.v1"
rfc = "0005"
name = "key_effects_writes_exact"
status = "implemented"
doc = "Key effect inference and writes_exact enforcement for branching meta-tasks"
payload_schema = '''
{
  "type": "object",
  "additionalProperties": false
}
'''
```

### 15.2 Writes Contract Design (Step 12.2)

The implementation uses a **unified Writes Contract** model:

```
Writes Contract = UNION( Keys(writes), writes_effect )
```

| Field | Purpose | When to Use |
|-------|---------|-------------|
| `writes` | Fixed/static key IDs | Task always writes the same columns |
| `writes_effect` | Dynamic/param-dependent | Columns depend on params |

**Key insight:** Most tasks only fill ONE field:
- Source tasks with fixed schema → use `.writes`
- Row-only ops (filter, take) → both empty
- Param-dependent ops (vm) → use `.writes_effect`

#### TaskSpec Fields

```cpp
struct TaskSpec {
  // ... other fields ...
  std::vector<KeyId> writes;                     // Static/fixed key writes
  std::optional<WritesEffectExpr> writes_effect; // Dynamic/param-dependent
};
```

#### Effect Expression Types (`engine/include/writes_effect.h`)

```cpp
// Keys{key_ids} → always Exact({keys})
struct EffectKeys { std::vector<uint32_t> key_ids; };

// FromParam("out") → Exact if param constant, else Unknown
struct EffectFromParam { std::string param; };

// SwitchEnum(param, cases) → Exact if param constant, May if bounded, else Unknown
struct EffectSwitchEnum {
  std::string param;
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
};

// Union([e1, e2, ...]) → combines effects
struct EffectUnion { std::vector<std::shared_ptr<WritesEffectExpr>> items; };

using WritesEffectExpr = std::variant<EffectKeys, EffectFromParam, EffectSwitchEnum, EffectUnion>;
```

#### Helper Function

`compute_effective_writes(spec)` computes the union automatically:
- If only `.writes` → `EffectKeys{writes}`
- If only `.writes_effect` → `*writes_effect`
- If both → `EffectUnion{Keys(writes), *writes_effect}`
- If neither → `EffectKeys{}` (empty, no writes)

### 15.3 Engine Evaluation (Step 12.3a)

The engine evaluates the writes contract **per-node at plan-load time** during `validate_plan()`.

#### Node Struct Fields

```cpp
struct Node {
  // ... existing fields ...

  // RFC0005: Evaluated writes contract (populated during validation)
  EffectKind writes_eval_kind = EffectKind::Unknown;
  std::vector<uint32_t> writes_eval_keys;  // sorted, deduped; empty for Unknown
};
```

#### Evaluation Flow

1. `validate_plan(plan)` iterates over nodes
2. For each node:
   - `validate_params()` returns `ValidatedParams` (with defaults + type normalization)
   - `build_param_bindings(validated_params)` extracts Int/String params as `EffectGamma`
   - `compute_effective_writes(spec)` combines `.writes` and `.writes_effect`
   - `eval_writes(expr, param_bindings)` evaluates to `WritesEffect{kind, keys}`
   - Result stored in `node.writes_eval_kind` and `node.writes_eval_keys`

#### CLI Output (`--print-plan-info`)

```bash
engine/bin/rankd --print-plan-info --plan_name reels_plan_a
```

Output (JSON):
```json
{
  "plan_name": "reels_plan_a",
  "capabilities_required": [],
  "capabilities_digest": "",
  "nodes": [
    {"node_id": "n0", "op": "viewer.follow", "writes_eval": {"kind": "Exact", "keys": [3001, 3002]}},
    {"node_id": "n1", "op": "vm", "writes_eval": {"kind": "Exact", "keys": [2001]}},
    {"node_id": "n2", "op": "filter", "writes_eval": {"kind": "Exact", "keys": []}},
    {"node_id": "n3", "op": "take", "writes_eval": {"kind": "Exact", "keys": []}}
  ]
}
```

For plans with unsupported capabilities, `--print-plan-info` returns exit code 1 with structured error:
```json
{
  "error": {
    "code": "UNSUPPORTED_CAPABILITY",
    "unsupported": ["cap.foo.v1"]
  }
}
```

### 15.4 Tests (Step 12.3b)

Test fixtures in `engine/tests/fixtures/plan_info/`:
- `vm_and_row_ops.plan.json` - vm with out_key, filter, take
- `fixed_source.plan.json` - viewer.fetch_cached_recommendation

Catch2 tests in `engine/tests/test_plan_info_writes_eval.cpp`:
- vm node: Exact + includes out_key
- filter/take/concat: Exact + empty keys
- Source tasks: Exact + includes fixed writes
- All keys sorted and unique

CI integration tests 55-56 verify CLI output.

### 15.5 Key Differences from RFC Proposal

| RFC Proposal | Actual Implementation |
|--------------|----------------------|
| `key_effects` field in artifact JSON | Evaluated at plan-load time, stored in Node struct |
| Lowercase kind: `"exact"`, `"may"` | PascalCase: `"Exact"`, `"May"`, `"Unknown"` |
| Compiler emits metadata | Engine computes during `validate_plan()` |
| Separate `writes` expression | Unified: `UNION(writes, writes_effect)` |

### 15.6 Files

| Component | Location |
|-----------|----------|
| Capability registry | `registry/capabilities.toml` |
| Effect types | `engine/include/writes_effect.h` |
| Effect evaluator | `engine/src/writes_effect.cpp` |
| TaskSpec with writes | `engine/include/task_registry.h` |
| Plan validation | `engine/src/executor.cpp` |
| CLI output | `engine/src/main.cpp` |
| Task guide | `docs/TASK_IMPLEMENTATION_GUIDE.md` |
| Tests | `engine/tests/test_plan_info_writes_eval.cpp` |
