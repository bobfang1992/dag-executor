---
rfc: 0002
title: "Timeout Wrapper Meta-Task (Region Deadline + Deterministic Fallback)"
status: Draft  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-13
updated: 2026-01-13
authors:
  - "<name>"
approvers:
  - "<name>"
requires: [0001]
replaces: []
capability_id: "cap.rfc.0002.timeout_wrapper.v1"
---

# RFC 0002: Timeout Wrapper Meta-Task (Region Deadline + Deterministic Fallback)

## 0. Summary
This RFC introduces a **meta-task** `timeout_wrapper` that enforces a **region-level wall-clock deadline** for a subgraph and
provides a deterministic policy on timeout:
- **Fail the plan**, or
- Execute a **fallback subgraph**.

The wrapper preserves the system’s **fail-closed** posture and keeps downstream semantics **deterministic** by requiring:
1) **RowSet shape is preserved** (v1: enrichment-only; no filter/sort/take/join/dedupe),
2) The set of written keys for both branches is **compile-time exact** and **equal**
   (`writes_exact(success) == writes_exact(fail)`),
3) Success branch writes are executed under **transactional semantics** (commit-or-rollback) so partial writes never leak.

Usage is gated by capability **`cap.rfc.0002.timeout_wrapper.v1`** (Scheme B, RFC 0001).

## 1. Motivation
Ranking pipelines often include “optional enrichment” or “best-effort scoring” regions whose latency can dominate tail latency.
Operators need a **predictable latency bound** while still returning a structured response.
Per-task timeouts are too granular and lead to scattered configuration and inconsistent downgrade behavior.

We want a single, reviewable, auditable mechanism that:
- Provides a **region deadline** (not just per-task budgets),
- Maintains deterministic downstream contracts (same keys; same rowset shape),
- Avoids “half-written keys” on timeout,
- Integrates with tracing and critical-path tooling.

## 2. Goals
- G1: Provide a **region-level wall-clock deadline** for a subgraph.
- G2: Provide deterministic timeout behavior: **fail plan** or **run fallback**.
- G3: Preserve **RowSet shape** in v1 (enrichment-only) to avoid control-flow/merge complexity.
- G4: Enforce deterministic downstream contracts via **exact, equal writes** for success/fallback.
- G5: Ensure no partial writes leak via **transactional overlay** semantics.
- G6: Integrate with observability (trace/audit) and link-time validation (fail-closed).

## 3. Non-Goals (v1)
- NG1: Catching arbitrary errors (logic/validation/type). Only timeout is catchable.
- NG2: Supporting rowset-shape mutations inside the wrapped region (filter/sort/take/join/dedupe/concat that changes row count).
- NG3: IO reliability patterns (retry/hedging/circuit breaker). These are separate meta-tasks.
- NG4: Accepting runtime-unknown outputs; wrapper requires `writes_exact` for both branches.

## 4. Proposal (high level)
Introduce a new meta-task op: `timeout_wrapper`.

A `timeout_wrapper` node references two callable subgraphs:
- `if_success`: subgraph to execute under a region deadline,
- `if_fail`: subgraph to execute if and only if `if_success` times out (when configured to fallback).

The wrapper computes a deadline using a row-aware budget model:
- `timeout_ms = clamp(min_ms, base_ms + per_row_us * active_rows / 1000, max_ms)`

Optionally, it can apply an override budget from a param, but:
- Override is **clamped**,
- In **prod**, override **must not** come from request-level values (policy is enforced fail-closed).

On timeout:
- If `on_timeout = "fail_plan"` → fail the request/plan.
- If `on_timeout = "fallback"` → rollback success writes and run `if_fail`.

## 5. Detailed Design

### 5.1 Capability gating (Scheme B)
Artifacts that contain `timeout_wrapper` MUST include capability:
- `cap.rfc.0002.timeout_wrapper.v1`

The engine must fail-closed if the capability is required but unsupported. See RFC 0001.

### 5.2 Authoring surface (TS)
The authoring API SHOULD resemble fragment authoring (fluent chaining), but does not require the user to hand-author fragments.

Example:

```ts
plan.timeout_wrapper({
  if_success: (c, ctx) =>
    c.fetch_features({ stage: "esr" })
     .call_models({ stage: "esr", out: Key.score_model })
     .vm(Key.score_final, dsl.add(Key.score_model, 1)),

  if_fail: (c, ctx) =>
    c.fill_defaults({ keys: [Key.score_model, Key.score_final] }),

  budget: { base_ms: 2, per_row_us: 20, min_ms: 1, max_ms: 15 },

  override: {
    param: P.override_budget_ms,
    allow_sources: ["plan", "fragment"], // prod default
    min_ms: 1,
    max_ms: 20,
    combine: "min",
  },

  on_timeout: "fallback",
});
```

Notes:
- `fill_defaults` is an infra-owned helper task (see §5.7) to make “shape-equal” fallbacks easy.
- V1 enforces `rowset_contract = preserve` (no rowset-changing ops).

### 5.3 Compile-time lowering (Scheme A: lift subgraph to fragments)
Although authoring may be inline, **dslc MUST lower** `if_success` and `if_fail` to callable subgraphs by lifting them into
anonymous fragment artifacts (content-addressed by digest).

Lowering requirements:
- Each branch becomes a closed, callable fragment: it may only read keys available at wrapper input and params resolved at link-time.
- Branch fragments are emitted deterministically and referenced by digest.
- SourceRef/callsite spans must map runtime errors back to the original TS location.

This lowering makes wrapper execution unambiguous (sequential branch execution within a transactional scope) without requiring nested DAG schemas.

### 5.4 IR / JSON representation
This RFC introduces a new node `op` value: `timeout_wrapper`.
The base Plan/Fragment JSON schema does not otherwise change.

#### 5.4.1 Node shape
```jsonc
{
  "node_id": "n123",
  "op": "timeout_wrapper",
  "params": {
    "success": {
      "fragment_digest": "sha256:...",
      "args": { /* resolved fragment args (link-time constants only) */ }
    },
    "fail": {
      "fragment_digest": "sha256:...",
      "args": { /* resolved fragment args */ }
    },

    "budget": {
      "base_ms": 2,
      "per_row_us": 20,
      "min_ms": 1,
      "max_ms": 15
    },

    "override": {
      "param_id": "param.override_budget_ms",
      "allow_sources": ["plan", "fragment"],
      "min_ms": 1,
      "max_ms": 20,
      "combine": "min"
    },

    "on_timeout": "fallback", // or "fail_plan"
    "rowset_contract": "preserve"
  },

  "trace": "optional-trace-string",
  "source_span": { /* SourceRef */ }
}
```

Defaults:
- `rowset_contract` defaults to `"preserve"` and v1 only supports `"preserve"`.
- `on_timeout` defaults to `"fail_plan"`.
- `override` is optional; if absent, only the model budget is used.

### 5.5 Validation rules (dslc + engine; fail-closed)
All validation is performed in both dslc and the engine (belt-and-suspenders).

#### 5.5.1 Branch closure
- Branch subgraphs must be closed with respect to node dependencies (no references to outer node_ids).
- Branch reads must only reference keys available at wrapper input.
- Branch params that influence semantics MUST be link-time constants (see §5.5.3).

#### 5.5.2 RowSet contract (v1)
V1 requires `rowset_contract = "preserve"`.

A branch is preserve iff:
- It contains no rowset-mutating ops (at minimum: `filter`, `sort`, `take`, `dedupe`, `join`, and any op declared rowset-mutating).
- All tasks used in the branch are declared rowset-preserving by their TaskSpec (or are in the engine’s preserve allowlist).
- If a task’s rowset effect is unknown, validation fails.

Runtime assert:
- The engine MUST assert that active row count, selection, and permutation are unchanged across branch execution.

#### 5.5.3 Exact key effects (deterministic shape)
For both branches, the set of written keys MUST be derivable as an **exact set** at compile/link time:

- For every node inside success/fail branches, `writes_exact(node)` must be computable.
- If any node is only `writes_may` (e.g., output keys depend on runtime param/request), validation fails.

Examples of disallowed patterns inside the wrapper (v1):
- `fetch_features(stage=P.stage)` where `P.stage` is request-bound.
- Any “param switch” that changes which bundle key is written unless the param is link-time constant.

#### 5.5.4 Shape equality
Let `W_success` and `W_fail` be the exact sets of keys written by each branch.

Requirement:
- `W_success == W_fail`

If not equal, validation fails with a diff listing missing/extra keys per branch.

#### 5.5.5 Override policy
If `override` is present:
- `override.param_id` must resolve to a number of milliseconds.
- The resolved param’s **origin** must be in `override.allow_sources`.
- In prod, `override.allow_sources` MUST NOT include request-origin values.
- If origin policy is violated: fail-closed.

Clamping:
- `override_ms = clamp(min_ms, override_value, max_ms)`.
- If clamp config is missing: fail-closed.

Combine:
- `"min"`: `timeout_ms = min(model_ms, override_ms)` (recommended default)
- `"replace"`: `timeout_ms = override_ms` (allowed only if explicitly configured)

### 5.6 Execution semantics (engine)
#### 5.6.1 Deadline computation
Let `n = active_row_count(input_rowset)`.

- `model_ms = base_ms + (per_row_us * n) / 1000`
- `model_ms = clamp(min_ms, model_ms, max_ms)`
- If override exists: compute `override_ms` per §5.5.5 and combine.
- Final: `timeout_ms` defines a region deadline `deadline = now + timeout_ms`.

#### 5.6.2 Transactional writes
The engine MUST execute the success branch under a transactional write overlay:
- All writes are staged in an overlay (copy-on-write for columns/keys).
- On success completion before deadline: **commit**.
- On timeout: **rollback** (no writes leak).

#### 5.6.3 Timeout detection
Timeout is defined as wall-clock deadline exceeded.
- Tasks inside the region should receive an execution context that exposes `deadline` / `time_remaining_ms()` and a cancellation check.
- The region deadline bounds each child task’s effective deadline: `child_deadline = min(task_deadline, region_deadline)`.

#### 5.6.4 Branch selection
- If success finishes before deadline: commit; return.
- If success times out:
  - If `on_timeout = "fail_plan"`: return Timeout error (fail request/plan).
  - If `on_timeout = "fallback"`:
    - rollback success overlay,
    - execute fail branch from the original input rowset,
    - commit fail writes (fail branch still runs under normal per-task budgets; it does not inherit the expired deadline unless configured separately).

#### 5.6.5 Error handling posture
Only timeout triggers fallback.
All other errors (validation failures, type errors, budget violations other than wall-clock timeout) fail the plan and do not run fallback.

### 5.7 Helper task: `fill_defaults`
To make shape-equal fallback practical, infra SHOULD provide a task:

- `fill_defaults(keys: KeyId[])`

Semantics:
- For each key in `keys`:
  - If key is nullable: write null
  - Else: write registry default (fail if missing)

This task is rowset-preserving and has deterministic behavior.

### 5.8 Observability and audit
The engine MUST emit wrapper-level trace fields:
- `timeout_ms`, `nrows`
- `success_elapsed_ms`, `timed_out` (bool)
- `chosen_branch`: `"success" | "fail" | "fail_plan"`
- `fail_elapsed_ms` (if executed)
- `override_applied`: bool, `override_origin` (plan/fragment/request/source/default/null)
- `W_success` size (optional), key ids list (optional; may be large)

Child task spans should be nested under a wrapper span, and their timestamps should contribute to critical path tooling.
On timeout failures, the error payload must include the wrapper node_id/op/trace and the computed timeout_ms.

### 5.9 Caching impact
- Usage of `timeout_wrapper` requires capability gating (RFC 0001).
- Plan binary cache keys MUST include the capabilities digest.
- The wrapper introduces no additional caching semantics beyond existing plan/fragment digests.
- If the branches include IO tasks in the future, caching policy must be revisited (out of scope v1).

### 5.10 Security / abuse resistance
- Override is clamped and origin-restricted (prod disallows request-origin budget loosening).
- Rowset-preserve v1 reduces the risk of semantic divergence and complexity.
- Transactional overlay prevents partial writes from leaking during timeouts.

## 6. Backward compatibility
- Artifacts without `timeout_wrapper` are unaffected.
- Engines that do not support this feature will fail-closed via capability gating.

## 7. Alternatives considered
1) **Per-task `on_timeout` configuration**
   - Simple but leads to configuration sprawl and does not support region budgets.
2) **Flattened control-flow DAG (mux/merge)**
   - Higher complexity in IR and runtime scheduling; hard to reason about merge semantics.
3) **Implicit auto-defaults on fallback**
   - Convenient but reduces review transparency; this RFC prefers explicit shape-equal fallback (`fill_defaults`) and exact outputs.

## 8. Risks and mitigations
- R1: Wrapper becomes hard to use due to exact-key restrictions.
  - Mitigation: Provide `fill_defaults`; require link-time constants for key-affecting params; keep v1 scope to enrichment-only.
- R2: Hidden partial writes on timeout.
  - Mitigation: Transactional overlay is mandatory; add unit tests for rollback correctness.
- R3: Override budgets become a DoS vector.
  - Mitigation: Clamp + origin policy; prod disallows request-origin loosening; trace override usage.

## 9. Test plan
- Unit tests:
  - Transactional overlay commit/rollback: no partial writes on timeout.
  - Exact writes inference fails when outputs depend on runtime params.
  - Shape equality enforcement: success/fail key sets must match.
  - Override clamp + origin policy enforcement.
  - Only timeout triggers fallback; other errors fail plan.
- Integration tests:
  - A sample plan where success intentionally times out; verify fallback branch runs and keys shape matches.
  - Trace includes wrapper fields and correct branch selection.
  - Rowset shape unchanged across wrapper execution.

## 10. Rollout plan
- Step 1: Implement engine `timeout_wrapper` op with transactional overlay + deadline computation.
- Step 2: Implement dslc lowering (lift branches to anonymous fragments) + validations (exact writes, preserve rowset, shape equality).
- Step 3: Add `fill_defaults` helper task.
- Step 4: Enable in dev/test; confirm trace/audit; then gate in prod via capability allowlist.

## 11. Open questions (future work)
- OQ1: Allow additional catch types (cpu/step budgets) while still disallowing mem-based fallback.
- OQ2: Extend beyond rowset-preserve (support filter/sort/take) with explicit merge semantics.
- OQ3: Add optional “region-local parallelism” semantics for executing branch subgraphs.
- OQ4: Provide a first-class syntax for common fallback patterns (e.g., default outputs) without reducing review transparency.
