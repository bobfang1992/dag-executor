---
rfc: 0001
title: "Extensions + Capability Gates for IR Evolution"
status: Implemented  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-13
updated: 2026-01-13
authors:
  - "<name>"
approvers:
  - "<name>"
requires: []
replaces: []
capability_id: "cap.rfc.0001.extensions_capabilities.v1"
---

# RFC 0001: Extensions + Capability Gates for IR Evolution

## 0. Summary
This RFC introduces a stable, fail-closed mechanism to evolve the Plan/Fragment JSON IR **without frequently changing the base schema**.
Artifacts declare a sorted, unique list of required capabilities (`capabilities_required`) and place any RFC-defined payload under
`extensions[capability_id]`. The engine must fail-closed if it cannot satisfy required capabilities or if an extension payload does not
validate. Caching must include a deterministic digest of the capability set + extension payloads.

## 1. Motivation
The repo freezes **spec.md** as an MVP baseline contract. We still need to ship new Tasks, scheduling semantics, tracing fields, and
validation rules over time. Bumping a global `schema_version` for every additive change quickly creates compatibility and tooling drag.

We want:
- A stable base wire format that remains readable and reviewable.
- Additive, namespaced evolution that is auditable and revertible.
- Fail-closed enforcement so “unknown semantics” cannot silently run.
- Deterministic hashing so caches cannot be poisoned by non-canonical JSON.

## 2. Goals
- G1: Provide a single, consistent place to hang new RFC-defined JSON fields.
- G2: Preserve fail-closed validation posture by capability gating.
- G3: Keep Plan/Fragment base schema stable for long stretches.
- G4: Make caching deterministic across languages (TS/JS, C++23, Rust).

## 3. Non-Goals
- NG1: Backward-compatible execution of plans that require unknown capabilities.
- NG2: A general-purpose plugin system for untrusted code.
- NG3: Solving all schema evolution issues (this defines the mechanism only).

## 4. Proposal (high level)
Add two optional fields to Plan JSON and Fragment JSON:
- `capabilities_required: string[]` — sorted, unique list of capability IDs required to interpret the artifact.
- `extensions: object` — map from capability ID → RFC-defined JSON payload.

Also allow an optional `extensions` map at the **node level** for node-scoped config, still gated by plan-level `capabilities_required`.

Unknown fields are disallowed everywhere except within `extensions[...]` payload objects, which are validated per capability.

## 5. Detailed Design

### 5.1 Capability IDs
A capability ID is a stable string:
- Format: `cap.rfc.NNNN.<slug>.vK` (K starts at 1)
- Example: `cap.rfc.0007.inprocess_quickjs_tasks.v1`

Rules:
- IDs are immutable once published.
- Any breaking change requires a new capability version (e.g. `.v2`).

### 5.2 Plan JSON shape (additive)
The base Plan schema adds the following optional fields with defaults:

```jsonc
{
  "schema_version": 1,

  // NEW (optional)
  "capabilities_required": [
    "cap.rfc.0007.inprocess_quickjs_tasks.v1"
  ],
  "extensions": {
    "cap.rfc.0007.inprocess_quickjs_tasks.v1": {
      "...": "RFC-defined payload"
    }
  }

  // ... existing plan fields ...
}
```

Defaults:
- If omitted: `capabilities_required = []`, `extensions = {}`.

Validation (engine and dslc):
- `capabilities_required` MUST be **sorted lexicographically** and **unique**.
- Every key in `extensions` MUST be present in `capabilities_required`.
- If an artifact requires a capability the engine does not support → **fail-closed**.

### 5.3 Fragment JSON shape (additive)
Fragments may also declare capabilities + extensions:

```jsonc
{
  "schema_version": 1,
  "capabilities_required": ["cap.rfc.0012.some_feature.v1"],
  "extensions": {
    "cap.rfc.0012.some_feature.v1": { "...": "payload" }
  }
  // ... fragment graph ...
}
```

Link-time merge rules (plan + resolved fragments):
- Required capabilities = union(plan, all fragments) → sort unique.
- Extensions merge by capability ID:
  - If a capability payload is provided by multiple artifacts, the payloads must be **deep-equal** (after canonicalization) or link fails.
  - If only one provides payload, use it.

Rationale: avoids “split-brain” config for the same semantic gate.

### 5.4 Node-level extensions (optional)
Nodes may include `extensions` for node-scoped config:

```jsonc
{
  "node_id": "n42",
  "op": "sort",
  "params": { "by": "Key.score", "order": "desc" },
  "extensions": {
    "cap.rfc.0020.sort_stability_hints.v1": { "stable": true }
  }
}
```

Rules:
- Any node-level `extensions` key MUST be present in the **linked plan** `capabilities_required`.
- Node-level extension payload validation is owned by the capability’s schema.

### 5.5 Fail-closed field policy
- The Plan/Fragment base schemas remain **strict**: unknown fields outside `extensions` cause validation failure.
- `extensions[...]` payloads are also strict: unknown fields inside a payload are rejected unless the RFC schema allows them.

This ensures no “stealth semantics” can be smuggled through loose parsing.

### 5.6 Deterministic digesting (for caching)
To prevent cache-key instability, the engine MUST compute a deterministic digest of capability requirements + extension payloads.

Definition (normative):
- Canonicalize JSON using **JSON Canonicalization Scheme (JCS, RFC 8785)**.
- Build a canonical object:

```jsonc
{
  "capabilities_required": [...],
  "extensions": { ... }
}
```

- Compute `capabilities_digest = sha256(jcs_bytes(canonical_object))`.

Cache key impact:
- The binary cache key MUST include `capabilities_digest` (in addition to existing plan/fragment/registry/task digests).

### 5.7 Observability
Recommended (not required for the mechanism):
- Include `capabilities_required` (and optionally `capabilities_digest`) in request-level audit logs.
- On failure due to missing capability, error payload MUST include the missing capability ID.

### 5.8 Compatibility story
- Old artifacts (no fields) still validate with defaults.
- New artifacts that require capabilities will fail on engines that do not support them (intentional fail-closed).

### 5.9 Security / abuse resistance
- Strict parsing outside `extensions` prevents accidental acceptance of unknown fields.
- Capability gating prevents execution under unknown semantics.
- Canonical digest prevents attackers from producing semantically identical but hash-different payloads to evade caches.

## 6. Alternatives considered
1) **Schema-version bump for every change**
   - Simple, but leads to version churn and multi-version tooling burden.
2) **Loose parsing with “ignore unknown fields”**
   - Breaks fail-closed posture; allows silent semantic drift.
3) **Per-task versioning only**
   - Does not cover cross-cutting semantics (tracing, scheduling, caching, governance rules).

## 7. Risks and mitigations
- R1: Overuse of extensions for things that should just be Task params.
  - Mitigation: RFCs should prefer normal params when semantics are local and do not need gating.
- R2: Merge conflicts across fragments become annoying.
  - Mitigation: deep-equal rule is deterministic; recommend keeping payloads small and centralized when possible.

## 8. Test plan
- Parsing:
  - Missing fields default correctly.
  - `capabilities_required` must be sorted/unique.
  - `extensions` keys must be subset of `capabilities_required`.
- Capability enforcement:
  - Unknown required capability fails with explicit error.
- Merge/link:
  - Union required capabilities stable ordering.
  - Conflicting payloads fail; identical payloads succeed.
- Digest:
  - Same semantic JSON → same `capabilities_digest` across languages.

## 9. Rollout plan
- Step 1: Implement optional fields + validation in engine and dslc (no behavior change for existing plans).
- Step 2: Update cache key to include `capabilities_digest`.
- Step 3: Future RFCs use capability IDs and store payload in `extensions`.

## 10. Implementation Reference

This RFC has been implemented in Steps 11.1-11.3:

### DSL/dslc (TypeScript)
- **Runtime**: `dsl/packages/runtime/src/plan.ts` - `ctx.requireCapability()`, node extensions
- **Validation**: `dsl/packages/runtime/src/artifact-validation.ts` - Shared artifact validation
- **Index**: `dsl/tools/build_all_plans.ts` - `computeCapabilitiesDigest()`, index generation

### Engine (C++)
- **Registry**: `engine/src/capability_registry.cpp` - `capability_is_supported()`, `validate_capability_payload()`, `compute_capabilities_digest()`
- **Parsing**: `engine/src/plan.cpp` - Parse capabilities_required, extensions, node extensions
- **Validation**: `engine/src/executor.cpp` - Reject unsupported capabilities
- **CLI**: `engine/src/main.cpp` - `--print-plan-info` flag

### Adding New Capabilities
See [docs/ADDING_CAPABILITIES.md](../docs/ADDING_CAPABILITIES.md) for the workflow.

## 11. Open questions
- Q1: Do we require fragments to avoid providing extension payloads (plan-only), or keep the merge rule as specified?
- Q2: Do we want a global allowlist of capability IDs in prod environments (similar to task allowlists)?
