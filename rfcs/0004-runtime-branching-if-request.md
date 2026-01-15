---
rfc: 0004
title: "Runtime Request Branching and Compile-time Guardrails for Control Flow"
status: Draft  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-14
updated: 2026-01-14
authors:
  - "<name>"
approvers:
  - "<name>"
requires: [0001]
replaces: []
capability_id: "cap.rfc.0004.if_request_branching.v1"
---

# RFC 0004: Runtime Request Branching and Compile-time Guardrails for Control Flow

## 0. Summary
This RFC addresses a common authoring pitfall and introduces a first-class solution:

1) **Compile-time guardrails**: prevent plan authors from using JavaScript/TypeScript control flow (`if`, `?:`, `&&`, `||`, `switch`) as if it were *runtime* branching. Since plans are built at compile-time, conditions that depend on runtime parameters (`P.*`) or data placeholders are invalid and must fail-closed with actionable errors.

2) **`if_request` meta-task**: provide a first-class, IR-level runtime branch operator:
`if_request(cond_bool, if_true=subgraph_a, if_false=subgraph_b)`

`if_request` evaluates `cond_bool` once per request and executes exactly one branch, under strict, deterministic contracts:
- v1 requires **RowSet shape preserved** (enrichment-only),
- branch outputs must be **compile-time exact** and **shape-equal** (`writes_exact(true) == writes_exact(false)`),
- branches are lowered by dslc into callable subgraphs (lifted fragments) with SourceRef mapping.

Usage is capability-gated via Scheme B (RFC 0001) under `cap.rfc.0004.if_request_branching.v1`.

## 1. Motivation
In this system, TypeScript is executed only to **build** a graph and emit JSON IR; it is not executed per request.
New authors frequently write:

```ts
if (P.use_new_model) { ... } else { ... }
```

This looks like runtime branching, but `P.use_new_model` is a placeholder object during plan build and is typically truthy,
so the plan silently becomes **one fixed branch**, creating confusing and unsafe behavior.

We need:
- A safe, deterministic way to express **request-level** branching (feature flags, migrations, rollouts),
- Clear, immediate feedback when authors attempt to use JS control flow incorrectly,
- Strong governance and observability for branch selection.

## 2. Goals
- G1: Provide a first-class `if_request` meta-task for request-level branching.
- G2: Enforce deterministic downstream contracts (same RowSet shape; same written keys) in v1.
- G3: Make incorrect JS control-flow usage fail-closed at compile time with precise SourceRef spans and a suggested fix.
- G4: Provide optional lint/IDE feedback to catch mistakes earlier (before invoking dslc).
- G5: Preserve the “plan is a DAG” mental model: `if_request` is explicit in IR and traceable.

## 3. Non-Goals (v1)
- NG1: Per-row branching (use value-level constructs like `set_if` / `coalesce` / PredIR/ExprIR for that).
- NG2: Allowing rowset-mutating ops inside branches (filter/sort/take/join) to influence the main pipeline’s RowSet.
- NG3: A general control-flow language with arbitrary loops or nested branching.
- NG4: Catch/try semantics (timeouts and fallbacks are handled by `timeout_wrapper`, RFC 0002).

## 4. Proposal (high level)
### 4.1 `if_request`
Add a new meta-task op `if_request` that:
- resolves a boolean condition (`cond_bool`) at runtime (per request),
- executes exactly one of two callable subgraphs,
- preserves RowSet shape in v1,
- requires both branches to have **exact and identical** `writes` sets.

### 4.2 Compile-time guardrails for JS control flow
dslc must validate authoring TS/JS AST:
- Any control-flow condition must be a **compile-time constant** (literal or allowed build-time constants such as `ctx.env`).
- If a condition references `P.*` (runtime params) or other runtime placeholders, compilation fails with an error that points to the condition span and recommends `if_request`.

## 5. Authoring surface (TypeScript)

### 5.1 Recommended runtime branching
A fluent API form (illustrative):

```ts
c.if_request({
  cond: P.use_new_model,              // boolean param
  if_true: (c) => c.call_models({ stage: "esr", out: Key.score_new })
                 .fill_defaults({ keys: [Key.score_old] }),

  if_false:(c) => c.call_models({ stage: "esr", out: Key.score_old })
                 .fill_defaults({ keys: [Key.score_new] }),

  trace: "model_rollout"
});
```

Notes:
- v1 enforces **rowset_preserve** in both branches.
- v1 enforces **shape equality** on written keys; `fill_defaults` is the standard way to match shapes.

### 5.2 Allowed compile-time branching (build-time constants only)
This is allowed when the condition is compile-time known:

```ts
if (ctx.env === "dev") {
  // dev-only nodes (not runtime)
}
```

### 5.3 Disallowed pattern (must fail-closed)
This must fail with a clear diagnostic:

```ts
if (P.use_new_model) { ... } else { ... }  // ❌ invalid: P.* is runtime
```

Error message must explain:
- plans are built at compile time,
- `P.*` is a runtime placeholder and cannot be used in JS conditions,
- recommended fixes: use `if_request(...)` or select between plan artifacts at the server/router layer.

## 6. Compilation model: lowering branches to callable subgraphs
dslc MUST lower `if_true` and `if_false` subgraphs into callable units by lifting them into anonymous fragment artifacts
(content-addressed by digest), identical to the lowering pattern used by `timeout_wrapper` (RFC 0002).

Lowering requirements:
- Branch subgraphs are **closed**: they may only read keys available at the `if_request` input point.
- Any params that influence branch **key effects** must be link-time constants *except* the boolean `cond` itself (which is runtime).
- SourceRef/callsite mapping must preserve error localization.

## 7. IR / JSON representation
This RFC introduces a new node `op`: `if_request`.

```jsonc
{
  "node_id": "n_if_1",
  "op": "if_request",
  "params": {
    "cond": { "param_id": "param.use_new_model" },
    "if_true":  { "fragment_digest": "sha256:...", "args": {} },
    "if_false": { "fragment_digest": "sha256:...", "args": {} },
    "rowset_contract": "preserve"
  },
  "trace": "model_rollout",
  "source_span": { /* SourceRef */ }
}
```

Defaults:
- `rowset_contract` defaults to `"preserve"` and v1 only supports `"preserve"`.

## 8. Validation rules (dslc + engine; fail-closed)
### 8.1 Condition type
- `cond` must resolve to boolean at runtime.
- `cond` origin may be request/plan/fragment per existing param binding rules (policy may restrict request overrides by env; out of scope v1).

### 8.2 RowSet contract (v1)
v1 requires `rowset_contract = preserve` for both branches:
- branches must not contain rowset-mutating ops (filter/sort/take/dedupe/join/concat that changes row count),
- tasks in branches must declare rowset-preserve in TaskSpec, otherwise validation fails.

Runtime assert:
- active row count, selection, and permutation are unchanged across branch execution.

### 8.3 Exact key effects and shape equality
Let `W_true` and `W_false` be the exact sets of keys written by each branch.

Requirements:
- Both branches must have **derivable exact writes** (no `writes_may`),
- `W_true == W_false` (shape equality).

If unequal, validation fails with a diff.

### 8.4 Reads safety
- Any key read in a branch must be present in the input schema at the `if_request` node.
- Reads can be derived from TaskSpec reads and ExprIR/PredIR references (if used within the branch).

## 9. Execution semantics (engine)
- Resolve `cond` once per request at `if_request` execution time.
- Execute exactly one branch subgraph in the current pipeline context.
- Errors in the chosen branch fail the plan (no fallback behavior; use `timeout_wrapper` for timeout-based fallback).

Observability:
- Emit `chosen_branch=true|false` on the `if_request` span, plus elapsed time and basic row counts.

## 10. Compile-time guardrails: disallow runtime placeholders in JS conditions
dslc MUST perform an AST validation pass over plan/fragment authoring code.

### 10.1 Guarded constructs
At minimum:
- `IfStatement.test`
- `ConditionalExpression.test` (`cond ? a : b`)
- `LogicalExpression` used in boolean contexts (common short-circuit branching): `&&`, `||`
- `SwitchStatement.discriminant` (if supported)

### 10.2 Allowed conditions (v1)
Conditions are allowed only if they can be proven compile-time constant, e.g.:
- boolean literals (`true`, `false`),
- comparisons over build-time constants (`ctx.env === "dev"`) where `ctx.env` is known at plan build time,
- other explicitly allowlisted build-time constants injected by the toolchain.

### 10.3 Disallowed conditions (must fail-closed)
Any condition that references runtime placeholders, including:
- `P.*` (ParamRef),
- request-dependent values,
- any opaque objects not provably constant.

### 10.4 Diagnostic requirements
Diagnostics must include:
- SourceRef span pointing to the condition,
- an explanation of compile-time vs runtime semantics,
- a suggested fix:
  - use `if_request(P.flag, ...)` for request-level branching, or
  - select between plan artifacts at the server/router layer for coarse rollout.

## 11. Lint/IDE guidance (recommended)
Provide an optional ESLint rule `no-paramref-in-conditions`:
- flags `if (P.x)`, `cond ? ... : ...`, and boolean contexts involving ParamRefs,
- suggests using `if_request` or `timeout_wrapper` depending on intent.

This is a DX enhancement; dslc enforcement remains the source of truth.

## 12. Caching impact
- Plans using this feature must require capability `cap.rfc.0004.if_request_branching.v1`.
- Binary cache key must include capabilities digest (RFC 0001).
- No additional cache-key changes are required beyond existing plan/fragment/registry digests.

## 13. Alternatives considered
1) **Use JS `if` for everything**
   - Incorrect for runtime branching; silently bakes one branch at build time; not auditable.
2) **Server/router selects between two plan artifacts**
   - Works, but splits governance/observability and complicates comparisons; still useful as an operational fallback.
3) **Flattened DAG with mux/merge**
   - Higher IR complexity and merge semantics; callable subgraph approach is simpler and consistent with other meta-tasks.

## 14. Risks and mitigations
- R1: Authors feel constrained by control-flow restrictions.
  - Mitigation: allow build-time `ctx.env` branching; provide first-class `if_request` for runtime needs.
- R2: Shape equality requirement is annoying.
  - Mitigation: provide `fill_defaults` helper task and clear diagnostics showing the mismatch.
- R3: Excessive branching complicates reasoning.
  - Mitigation: keep v1 limited (request-level, preserve rowset) and rely on explicit trace markers.

## 15. Test plan
### 15.1 dslc AST validation
- Reject `if (P.x)` with correct SourceRef and suggested fix.
- Allow `if (ctx.env === "dev")` when env is build-time known.
- Reject boolean contexts using ParamRefs (`P.x && ...`, `P.x ? ... : ...`).

### 15.2 Link-time validation
- Enforce rowset-preserve allowlist for branches.
- Enforce `writes_exact` for both branches.
- Enforce shape equality (`W_true == W_false`) with a clear diff.

### 15.3 Runtime execution
- Cond selects correct branch; only chosen branch tasks execute.
- Trace includes chosen_branch and timing.
- Errors in chosen branch fail plan.

## 16. Rollout plan
- Step 1: Add AST guardrails to dslc (fail-closed) and optional ESLint rule.
- Step 2: Implement `if_request` in engine (callable subgraph execution) + validations.
- Step 3: Expose TS authoring API and examples.
- Step 4: Gate usage in prod via capability allowlist and monitor adoption/misuse diagnostics.

## 17. Open questions
- OQ1: Allow rowset-mutating ops inside `if_request` with explicit merge semantics (likely v2+).
- OQ2: Allow multi-way branching (`switch_request`) for enums (v2+).
- OQ3: Policy on request-origin param overrides for `cond` in prod environments.
