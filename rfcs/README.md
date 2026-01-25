# RFC Process (Scheme B: extensions + capability gates)

This repo treats **spec.md** as the **frozen MVP baseline** (SSOT for the original contract).
All normative evolution happens via **RFCs** in this directory.

## Default evolution strategy: Scheme B
We keep the **base Plan/Fragment JSON schema stable** and evolve behavior via:

- `capabilities_required`: a sorted, unique list of capability IDs required by the artifact.
- `extensions`: a map keyed by capability ID containing the JSON payload defined by that RFC.

Outside of `extensions`, **unknown JSON fields are not allowed** (fail-closed).

`schema_version` exists, but should be bumped only for rare “base skeleton” changes.
Most changes should be additive capabilities under `extensions`.

## Directory + numbering
- RFCs live in `rfcs/`.
- Filenames: `NNNN-short-slug.md` (4-digit, zero-padded).
  - Example: `0001-extensions-capabilities.md`
- Numbers are monotonic; never reuse.

## Lifecycle / status
- Draft → Review → Accepted → Implemented → Final
- Rejected / Superseded remain for history.

## What qualifies as a “normative change”
Requires an RFC:
- Any new Task/op, IR node, or Plan/Fragment field (outside `extensions`)
- Any change to validation rules / defaults / lifecycle semantics
- Any change that affects runtime behavior, caching keys, audit/tracing

Does **not** require RFC (but still use PR review):
- Typos, formatting, link fixes in spec.md
- Editorial clarifications that do not change meaning

## Required “gates” for Accepted RFCs
Every Accepted RFC must specify:
- **Capability ID** (stable): `cap.rfc.NNNN.<slug>.v1`
- **Compatibility**: additive vs breaking; migration path
- **Validation**: what is fail-closed vs best-effort
- **Budgets**: time/cpu/mem/step limits; worst-case bounds
- **Observability**: trace/audit/log surfaces and SourceRef impact
- **Caching**: what digests/version bumps are required

## Consumption rule (engine/dslc)
- Plans/fragments that use a feature must list the RFC’s `capability_id` in `capabilities_required`.
- Any RFC-defined JSON payload must be stored under `extensions[capability_id]`.
- The engine **must fail-closed** if:
  - It does not support a required capability, or
  - The extension payload does not validate against that RFC’s schema.

## Template
Start from `0000-template.md`.

## RFC Index

| RFC | Title | Status | Capability ID |
|-----|-------|--------|---------------|
| [0001](0001-extensions-capabilities.md) | Extensions + Capability Gates | **Implemented** | `cap.rfc.0001.extensions_capabilities.v1` |
| [0002](0002-timeout-wrapper.md) | Timeout Wrapper Meta-Task | Draft | `cap.rfc.0002.timeout_wrapper.v1` |
| [0003](0003-debuggability-breakpoints-capture-after-request.md) | Debuggability: Breakpoints, Capture, Postlude | Draft | `cap.rfc.0003.debug_capture_postlude.v1` |
| [0004](0004-runtime-branching-if-request.md) | Runtime Request Branching | Draft | `cap.rfc.0004.if_request_branching.v1` |
| [0005](0005-key-effects-writes-exact.md) | Key Effects (writes_exact) | Draft | `cap.rfc.0005.key_effects_writes_exact.v1` |
| [0006](0006-plan-visualizer.md) | Plan Visualizer | Draft | `cap.rfc.0006.plan-visualizer.v1` |
| [0007](0007-namespaced-task-ops.md) | Namespaced Task Operation Names | **Implemented** | N/A (registry change) |

## Adding Capabilities

See [docs/ADDING_CAPABILITIES.md](../docs/ADDING_CAPABILITIES.md) for the implementation workflow.
