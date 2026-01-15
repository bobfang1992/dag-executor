---
rfc: 0005
title: "Key Effects and writes_exact: Compile-time Effect Inference for Deterministic RowSet Schema"
status: Draft  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-14
updated: 2026-01-14
authors:
  - "<name>"
approvers:
  - "<name>"
requires: [0001]
replaces: []
capability_id: "cap.rfc.0005.key_effects_writes_exact.v1"
---

# RFC 0005: Key Effects and `writes_exact` — Compile-time Effect Inference for Deterministic RowSet Schema

## 0. Summary
This RFC formalizes a lightweight **key effect system** over RowSet schema and defines the term **`writes_exact`** as a normative compile-time contract.

We introduce:
- A small, deterministic effect language for TaskSpec to describe how a task’s **written keys** depend on parameters.
- A three-valued effect result: `Exact(K) | May(K) | Unknown`.
- A compile/link-time evaluation procedure that produces `writes_exact` when (and only when) the effect is provably exact under link-time constants.
- Subgraph aggregation rules and validation points used by meta-tasks (e.g., `timeout_wrapper`, `if_request`) to enforce deterministic “shape equality”.

This RFC does **not** change the runtime execution model; it defines compile-time inference and validation rules and how they are consumed under Scheme B capability gating.

Capability gate: `cap.rfc.0005.key_effects_writes_exact.v1`.

## 1. Motivation
Several meta-features require deterministic and reviewable guarantees about RowSet schema:
- `timeout_wrapper` requires success/fallback branches to write the same keys deterministically.
- `if_request` requires both branches to preserve RowSet shape and write the same keys deterministically.
- Debug tooling benefits from compiler-produced “schema lens” information at each node.

Without a formal definition, “writes_exact” becomes an informal idea with inconsistent interpretations, leading to:
- divergent implementations across languages (TS/C++/Rust),
- silent behavior changes during refactors,
- weak or non-existent fail-closed validation for meta-tasks.

This RFC makes key effect inference a first-class, testable contract.

## 2. Goals
- G1: Define `writes_exact` formally and make it decidable under a restricted effect language.
- G2: Enable fail-closed validation when a feature requires deterministic schema (e.g., branch shape equality).
- G3: Support tasks whose written keys depend on a small set of enumerated parameters (e.g., `stage`), without allowing arbitrary dynamic keys.
- G4: Provide a uniform way to compute:
  - `writes_exact(node)` (when possible),
  - `writes_may(node)` (a safe finite over-approximation),
  - and to reject `Unknown` effects in restricted contexts.
- G5: Keep the effect system small enough to implement consistently in dslc and the engine.

## 3. Non-Goals (v1)
- NG1: Proving semantic properties of values (e.g., monotonicity, invariants) — only schema/key effects.
- NG2: Per-row (data-dependent) effects or branching — effects may depend only on link-time constants and bounded enums.
- NG3: Inferring effects from arbitrary user code — effects are declared in TaskSpec; inference only evaluates them.
- NG4: Full dependent typing over values — this is an effect system / refinement over RowSet schema, not general dependent types.

## 4. Definitions

### 4.1 Key, RowSet schema
- A **Key** is a registry-governed top-level field identified by stable `key_id`.
- A RowSet schema at a point in the graph can be modeled as a set of keys that are present (with types/nullability from the registry).

### 4.2 Compile/link-time environment Γ
Γ contains information known at compile/link time:
- literal constants in authoring code,
- fragment args after linking (resolved constants),
- plan-level constants (if supported),
- build-time constants such as `ctx.env` (if supported).

Γ does **not** include request-time values.

### 4.3 Effect results
Effect evaluation produces one of:

- `Exact(K)`:
  The task/subgraph will write exactly the finite set of keys `K`.
- `May(K)`:
  The task/subgraph may write some subset of `K`; `K` is a finite over-approximation.
- `Unknown`:
  A safe finite over-approximation is not available (or forbidden by policy).

Define helper projections:
- `keys(Exact(K)) = K`
- `keys(May(K))   = K`
- `keys(Unknown)  = ∅` (only for error reporting; not a valid over-approx)

### 4.4 `writes_exact`
`writes_exact(node)` is defined iff `writes_effect(node, Γ) = Exact(K)`.
In that case, `writes_exact(node) = K`.

Similarly:
- `writes_may(node) = K` if `writes_effect(node, Γ) ∈ {Exact(K), May(K)}`.
- `writes_may(node)` is undefined for `Unknown`.

## 5. TaskSpec: declarative key effect language

### 5.1 Requirement: no dynamic keys
TaskSpec MUST NOT allow tasks to create keys dynamically from strings or data.
All written keys must be expressible as a finite set of registry `key_id`s.

### 5.2 Effect expression forms (v1)
TaskSpec may declare `writes` using one of the following forms:

1) **Static set**
- `writes = Keys{K1, K2, ...}`

2) **From key-valued param**
- `writes = FromParam("out")`
  - The param value must be a `key_id` at compile/link time for `Exact`.

3) **Enum switch**
- `writes = SwitchEnum(param="stage", cases={ "esr": Keys{Key.features_esr}, "lsr": Keys{Key.features_lsr} })`
  - If `stage` is link-time constant → `Exact` of the selected case.
  - If `stage` is not constant but domain is a bounded enum → `May` of the union.
  - If domain is unknown/unbounded → `Unknown`.

4) **Union**
- `writes = Union([expr1, expr2, ...])`
  - Combines multiple write sources (e.g., fixed outputs plus `FromParam(out)`).

This restricted language is sufficient for common tasks:
- `vm`: `FromParam("out")`
- `extract_features`: `FromPath("map[].out")` (optional extension, see §5.3)
- stage-dependent bundle tasks via `SwitchEnum`.

### 5.3 Optional extension: path-based key lists
For params that contain arrays/objects with embedded keys, TaskSpec may declare:
- `FromPath("items[].out")`

Semantics match `FromParam`, but the param value is a finite list of key_ids.
`Exact` requires the list to be fully known at link time; otherwise `Unknown` (or `May` if bounded and enumerated).

## 6. Effect evaluation (normative)

### 6.1 Evaluation function
Define `EvalWrites(effect_expr, Γ) -> Exact(K) | May(K) | Unknown`.

Rules (v1):

- `EvalWrites(Keys{K...}, Γ) = Exact({K...})`.

- `EvalWrites(FromParam(p), Γ)`:
  - if Γ binds param `p` to a concrete key_id (or finite list via FromPath): `Exact({key_id...})`
  - else `Unknown` (unless a bounded domain is explicitly enumerated, in which case `May(union(domain))` is permitted by policy).

- `EvalWrites(SwitchEnum(param, cases), Γ)`:
  - if Γ binds `param` to a concrete enum value `v` and `v ∈ cases`: `Exact(keys(cases[v]))`
  - else if `param` is not constant but its domain is a finite enum and `cases` covers the full domain: `May(union over cases)`
  - else `Unknown`.

- `EvalWrites(Union([e1..en]), Γ)`:
  - evaluate each `ri = EvalWrites(ei, Γ)`
  - if any `ri = Unknown`: result is `Unknown`
  - else if all are `Exact`: `Exact(union(keys(ri)))`
  - else: `May(union(keys(ri)))`

### 6.2 Policy: where `May` is acceptable
The system must distinguish contexts:
- **Strong-shape contexts** (branching/fallback meta-tasks): `May` is not acceptable → must be `Exact`.
- **Lens/tooling contexts** (schema preview, dependency graph): `May` is acceptable as an over-approximation.
- **General compilation**: policy may allow `May` for warnings or tooling, but must not silently treat `May` as `Exact`.

## 7. Subgraph aggregation rules

### 7.1 Node-level effects
Each node has a `writes_effect(node, Γ)` derived by evaluating the TaskSpec writes expression with node params under Γ.

### 7.2 Subgraph writes
For a subgraph `S` containing nodes `{n1..nk}`, define:

- `writes_effect(S, Γ) = Union([writes_effect(n1, Γ), ..., writes_effect(nk, Γ)])`

So:
- `writes_exact(S)` exists iff every node’s effect is `Exact` (and no Unknown), and the union is `Exact`.

### 7.3 Overwrite / conflict constraints
This RFC does not redefine overwrite rules. However:
- When aggregating `writes_exact`, implementations MUST still apply existing governance rules (e.g., “no overwrite” or “only explicit out keys”).
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

## 9. Consuming `writes_exact` in meta-tasks (normative)

### 9.1 `timeout_wrapper` (RFC 0002)
V1 requires:
- `rowset_contract = preserve`
- `writes_exact(success) == writes_exact(fail)`

If either branch yields `May` or `Unknown`, validation MUST fail-closed with a diagnostic identifying the node/param causing non-exact effects.

### 9.2 `if_request` (RFC 0004)
V1 requires:
- `rowset_contract = preserve`
- `writes_exact(if_true) == writes_exact(if_false)`

Same failure posture as above.

### 9.3 Debug capture & postlude jobs (RFC 0003)
- Breakpoint naming and capture do not require `writes_exact`, but tooling may use `writes_may` to propose default column sets.
- Postlude allowlists may use `rowset_effect` rather than `writes_exact` for enforcement.

## 10. Diagnostics requirements (DX)
When `Exact` is required but not available, the compiler must produce:
- the specific node (op + node_id) and source span,
- which param caused non-exactness (e.g., `stage`),
- the computed `May{...}` set if applicable,
- and a suggested fix (e.g., “make stage a link-time constant” or “use fill_defaults to equalize outputs”).

Example message (conceptual):
> Cannot prove writes_exact for node n12 (fetch_features): param `stage` is runtime-bound. In this context, deterministic shape is required.
> Computed writes_may = {Key.features_esr, Key.features_lsr}. Fix: make `stage` a link-time constant (literal/fragment arg), or move this task outside the wrapper.

## 11. Compatibility and rollout
- This RFC introduces no breaking runtime changes.
- dslc and engine must agree on effect evaluation and diagnostics; add golden tests and cross-language fixtures.
- Capability gating: plans that rely on `writes_exact` in meta-task validations require `cap.rfc.0005.key_effects_writes_exact.v1` (directly or via dependent RFCs).

## 12. Test plan
### 12.1 Unit tests (dslc)
- `vm(out=Key.x)` yields `Exact({Key.x})`.
- `fetch_features(stage="esr")` yields `Exact({Key.features_esr})`.
- `fetch_features(stage=P.stage)` with bounded enum yields `May({Key.features_esr, Key.features_lsr})`.
- same case but unbounded domain yields `Unknown`.
- Union combination rules for Exact/May/Unknown.

### 12.2 Integration tests (meta-tasks)
- `timeout_wrapper` rejects branches with `May` effects.
- `if_request` rejects branches with mismatched Exact write sets.
- Diagnostics include node_id, param name, and computed may set.

### 12.3 Golden fixtures (cross-language)
- A JSON fixture describing a TaskSpec writes expression and a set of Γ bindings, with expected result.
- Run fixture tests in both dslc (TS) and engine (C++/Rust) implementations.

## 13. Alternatives considered
- Treat all param-dependent writes as Unknown and forbid them globally.
  - Too restrictive; common patterns like stage bundles become painful.
- Allow arbitrary dynamic keys.
  - Breaks governance and determinism; not acceptable.
- Full dependent type system over values.
  - Overkill; we only need schema/effect refinement.

## 14. Open questions
- OQ1: Formalize `reads_exact` / `reads_may` similarly (especially for ExprIR/PredIR references).
- OQ2: Extend effect language with bounded `FromPath` for common map/list patterns (extract_features).
- OQ3: Formalize rowset mutation effects and merge semantics for future control-flow features.
