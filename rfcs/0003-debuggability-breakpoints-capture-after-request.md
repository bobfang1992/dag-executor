---
rfc: 0003
title: "Debuggability: Breakpoints, RowSet Capture, and After-Request Postlude"
status: Draft  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-14
updated: 2026-01-14
authors:
  - "<name>"
approvers:
  - "<name>"
requires: [0001]
replaces: []
capability_id: "cap.rfc.0003.debug_capture_postlude.v1"
---

# RFC 0003: Debuggability — Breakpoints, RowSet Capture, and After-Request Postlude

## 0. Summary
This RFC introduces two complementary debuggability features:

1) **Breakpoints (`assign`) + request-triggered capture**:
   Plan/fragment authors label stable points in the pipeline with `.assign("<label>")`.
   Requests can then ask the engine to **capture the entire RowSet** at those breakpoints, producing a **capture_id** that tools can analyze offline.

2) **`after_request` postlude jobs**:
   Plan authors attach an **after-request mini-pipeline** (stats/transform/serialize/emit) to a breakpoint.
   The engine runs these jobs **after the critical path** (dev: synchronous postlude; prod: asynchronous queue) to support training-data logging and heavy serialization without impacting user latency.

Both features preserve the project’s core posture:
- runtime executes compiled IR,
- strict validation (fail-closed),
- deterministic naming and outputs,
- capability gating via Scheme B (RFC 0001).

## 1. Motivation
A recurring pain point in declarative DAG pipelines is reasoning about:
- What the candidate RowSet looks like at a specific stage (columns, nulls, distributions),
- Why a stage changed counts/order (filter/sort/take),
- How to produce reliable training-data logs without inflating P99 latency.

Node IDs are not user-friendly and are unstable under compilation/linking.
We need a first-class notion of **named stage points** (breakpoints) and a way to materialize the RowSet and/or derived artifacts for offline inspection and training pipelines.

## 2. Goals
- G1: Allow authors to label stable stage points using `.assign(label)` in both plans and fragments.
- G2: Allow requests to trigger capture by breakpoint name (not node_id), producing a **capture_id**.
- G3: Capture the **entire RowSet** (raw semantics: base_batch + selection + permutation + dicts) under strict limits and deterministic policies.
- G4: Support heavy work after the response is determined/flushed via `after_request` jobs (dev inline postlude; prod async queue).
- G5: Keep capture and postlude behavior deterministic, auditable, and fail-closed under policy violations.
- G6: Provide a clean path to log training data without contaminating the critical path.

## 3. Non-Goals (v1)
- NG1: Arbitrary user-specified node_id targeting for capture (breakpoints only).
- NG2: Turning `after_request` into a general-purpose compute platform (no join/model/feature fetch).
- NG3: Guaranteeing postlude execution (prod is best-effort under backpressure).
- NG4: Cross-request caching semantics for captured artifacts (out of scope).

## 4. High-level design overview
### 4.1 Breakpoints
- `.assign("label")` tags the **current RowSet output point** without creating a new runtime task.
- Labels are resolved into a **qualified breakpoint name** during compile/link:
  `plan_name::instance_path::label`

### 4.2 Two-phase capture
To keep heavy work off the critical path:
- **Phase A (freeze)**: at breakpoint time, create a lightweight **CaptureTicket** (O(1) ref-counting / COW).
- **Phase B (serialize/store)**: after the request completes:
  - dev: serialize/store synchronously in a postlude block (S1),
  - prod: enqueue a job to serialize/store asynchronously (S3).

### 4.3 After-request postlude jobs
- Authors attach `after_request` jobs inline near the stage they care about.
- dslc lowers them into `postlude_jobs[]` artifacts (lifted to fragments), anchored to breakpoints.
- Jobs run after the response, using the captured breakpoint snapshot as input.
- Job language is intentionally limited: **stats + transform (filter/take/sample/project) + serialize + emit**.

## 5. Authoring surface (TypeScript)

### 5.1 Breakpoints via `.assign()`
```ts
c.call_models({ stage: "esr", out: Key.score_model })
 .assign("after_models")
 .filter({ pred: (k) => k.score_model > 0.1 })
 .assign("after_filter");
```

Semantics:
- `.assign(label)` attaches a debug label to the “current” RowSet output point.
- It returns the same fluent handle (no semantic change to the plan).

### 5.2 Inline `after_request`
Attach a postlude job anchored to the most recent assigned breakpoint (or an implicit anchor if absent):

```ts
c.call_models({ stage: "esr", out: Key.score_model })
 .assign("after_models")
 .after_request(ar =>
   ar.filter((k) => k.score_model > 0.1)
     .sample({ rows: 5000 })
     .project([Key.id, Key.score_model, Key.country])
     .describe({ keys: [Key.score_model] })
     .serialize_rowset({ format: "arrow", mode: "raw" })
     .emit_training_log({ sink: TrainSink.esr_v1, ttl_days: 7 })
 );
```

Notes:
- `filter(...)` inside `after_request` uses **PredIR** (compiled and validated like normal).
- `sample` MUST be deterministic (seeded by run_id/request_id).
- `emit_training_log` is an allowlisted sink (see §9).

## 6. Breakpoint naming and scoping

### 6.1 Qualified breakpoint names
A local label is transformed into a qualified name:

`<plan_name>::<instance_path>::<label>`

- `plan_name`: declared in plan header (recommended), or a deterministic digest-based name if omitted.
- `instance_path`: concatenation of fragment instance aliases along the call chain.
- `label`: the literal string passed to `.assign()`.

### 6.2 Instance aliases and determinism
To prevent collisions when fragments are reused:
- Fragment invocations SHOULD provide an explicit alias (recommended):
  `use(fragment, { as: "esr" })`
- If no alias is provided, dslc MUST generate a deterministic alias from callsite SourceRef/span id:
  e.g. `__callsite_S123`.

### 6.3 Validation
Fail-closed at link time:
- Labels MUST match a restricted charset (recommended: `[A-Za-z0-9._-]`) and MUST NOT contain `::`.
- Qualified breakpoint names MUST be unique within a linked plan.
- A node with a breakpoint label acts as an optimization barrier for any cross-instance node merging that would break mapping stability.

## 7. Request-triggered capture

### 7.1 Request schema (conceptual)
Requests may include a capture directive by breakpoint name:

```jsonc
{
  "debug_capture": [
    {
      "breakpoint": "MyPlan::esr::after_models",
      "mode": "full_raw",          // stats | sample_raw | sample_view | full_raw | full_view
      "limits": {
        "max_bytes": 100000000,
        "max_rows": 50000,
        "max_columns": 2000
      },
      "sample": { "rows": 200 },
      "include_columns": "ALL",    // or explicit list / patterns (optional)
      "exclude_columns": ["feature_bundle"] // recommended default in prod
    }
  ]
}
```

### 7.2 Capture modes
- `stats`: compute schema + summary statistics only (no row data).
- `sample_raw`: capture RowSet raw representation, but only for a deterministic sample of rows.
- `sample_view`: apply selection/permutation and store a sampled materialized table.
- `full_raw`: store full raw RowSet (base_batch + selection + permutation + dicts).
- `full_view`: store full materialized view (more expensive).

Recommendation:
- Default `full_raw` in dev with strict limits.
- Default `sample_raw` or `stats` in prod.

### 7.3 Deterministic sampling
Sampling must be deterministic using a stable seed:
- seed = hash(run_id) (preferred) or hash(request_id)
- sampling occurs over the RowSet view semantics (selection/permutation), then stored as raw or view as requested.

### 7.4 Response payload
The request returns capture handles:

```jsonc
{
  "captures": [
    {
      "breakpoint": "MyPlan::esr::after_models",
      "capture_id": "cap_9a12...",
      "status": "queued" // dev may return "ready"; prod typically "queued"
    }
  ]
}
```

## 8. Runtime execution model

### 8.1 Phase A: Freeze at breakpoint
At the moment a breakpoint node completes:
- The engine creates a **CaptureTicket**:
  - references the RowSet snapshot (base_batch + selection + permutation),
  - records meta (breakpoint name, node_id, plan digest, registry digests, timestamps),
  - records estimated size for budgeting/backpressure.

Freeze MUST be O(1) in the common case (ref-count only), relying on the immutable/COW columnar model.

### 8.2 Phase B: Serialize/store after request
After the response is committed/flushed:

- **Dev mode (S1)**: run postlude synchronously:
  - serialize according to capture mode,
  - store to debug store,
  - return status `ready` with capture_id.

- **Prod mode (S3)**: enqueue to a postlude queue:
  - return status `queued` and capture_id immediately,
  - worker serializes/stores later and updates status to `ready`/`failed`/`expired`.

### 8.3 Backpressure and degradation policy
To prevent capture from harming system stability:
- enforce per-request limits: `max_bytes`, `max_rows`, `max_columns`.
- enforce global limits: `max_inflight_tickets_bytes`, `max_inflight_jobs`.
- deterministic degrade order on limit exceed:
  `full_* → sample_* → stats → drop`
- All degradation and drops must be logged with a machine-readable `drop_reason`.

## 9. `after_request` postlude jobs

### 9.1 Compilation model
Each `.after_request(ar => ...)` is lowered into a `postlude_job`:
- `anchor_breakpoint` (qualified name),
- `job_fragment_digest` (lifted anonymous fragment),
- `when` predicate (optional),
- budgets/limits,
- sinks.

dslc collects all postlude jobs into the plan’s `extensions` under this RFC capability.

### 9.2 Allowed task set (v1 allowlist)
Postlude job DAG may contain only:

**Stats**
- `describe(keys=...)`, `histogram`, `topk`, etc.

**Transform (RowSet-only)**
- `filter(PredIR)`
- `take(n)`
- `sample(rows, seed=run_id)`
- `project(keys=[...])`

**Serialization**
- `serialize_rowset(format, mode)`

**Emit (the only IO)**
- `emit_training_log(sink_id, ttl, payload_ref)`
- `emit_capture_store(store_id, ttl, payload_ref)`

Explicitly disallowed in v1:
- `join`, `call_models`, `fetch_features`, arbitrary IO, any task that depends on external services other than allowlisted emit sinks.

### 9.3 Inputs
A postlude job consumes the snapshot identified by `anchor_breakpoint`.
If multiple jobs share the same anchor, they may share the same frozen ticket.

### 9.4 Execution timing and failure semantics
- Jobs run after response completion.
- Job failure MUST NOT affect the request response.
- Failures must be observable: status, error code, and a stable job_id.

### 9.5 Budgets
Postlude jobs have independent budgets, enforced by the postlude worker:
- wall-clock timeout (per job),
- CPU/memory limits (implementation-defined),
- output size limits (bytes/rows/columns),
- sink-specific quotas.

## 10. Storage model and handles

### 10.1 Capture handles
A capture is addressed by `capture_id` (opaque, non-guessable).
`capture_id` resolves to a manifest describing:
- status: `queued|running|ready|failed|expired|dropped`
- artifact locations (object store paths, offsets),
- metadata (breakpoint, plan digest, schema info, timestamps).

### 10.2 Request_id indexing (optional)
Engines may maintain an index from `request_id` to recent `capture_id`s, but the stable handle for tooling is `capture_id`.

## 11. Observability
### 11.1 Trace fields (recommended)
For each capture:
- breakpoint name, node_id
- mode, limits, estimated size
- chosen degrade level (if any)
- freeze time, serialize time, bytes written
- queue wait time (prod)

For each postlude job:
- job_id, anchor_breakpoint
- status transitions, elapsed, bytes out
- sink id, emit latency, retries (if any)
- drop_reason / backpressure metrics

### 11.2 Debug tooling
Tooling SHOULD provide:
- list breakpoints for a plan (`plan describe --breakpoints`),
- fetch capture manifest by capture_id,
- convert raw captures to a materialized view for interactive analysis,
- diff two captures (optional future work).

## 12. Security and policy
- Captures and postlude emit sinks MUST be allowlisted by environment.
- Prod policy SHOULD default to stats/sample and exclude large/PII-heavy columns unless explicitly permitted.
- All captures must have TTL and be garbage-collected.
- Access control to captured artifacts is enforced by the store and service layer (implementation-defined).

## 13. Backward compatibility
- Plans without these features are unaffected.
- Engines that do not support this RFC will fail-closed via capability gating.

## 14. Alternatives considered
- Node-id targeting only: not user-friendly; unstable under compilation/linking.
- Synchronous serialization on the critical path: harms tail latency.
- Allowing arbitrary tasks in postlude: turns into a general compute platform; increases risk and complexity.

## 15. Test plan
- Compile/link tests:
  - breakpoint naming, scoping, uniqueness; deterministic aliases from callsite.
  - `.assign` produces stable breakpoint→node mapping.
  - `.after_request` lowers into postlude_jobs with correct anchors.
- Runtime tests:
  - Freeze is O(1) (no eager materialization) and does not change main pipeline semantics.
  - Dev mode: capture becomes ready after response; payload can be read and matches expectations.
  - Prod mode: capture/job transitions queued→ready/failed; backpressure causes deterministic degradation/drops.
  - Transform tasks in postlude (filter/take/sample/project) are deterministic and bounded.
  - Only allowlisted emit sinks are permitted; policy violations fail-closed at link/load time.

## 16. Open questions (future work)
- Support compare/shadow meta-tasks based on captures (diff rank/score distributions).
- Richer privacy controls (field-level redaction policies).
- Additional postlude triggers: on_slow, on_error, targeted user cohort sampling.
