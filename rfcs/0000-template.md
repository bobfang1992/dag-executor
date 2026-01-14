---
rfc: 0000
title: "<short title>"
status: Draft  # Draft | Review | Accepted | Implemented | Final | Rejected | Superseded
created: 2026-01-13
updated: 2026-01-13
authors:
  - "<name>"
approvers:
  - "<name>"
requires: []      # RFC numbers this builds on, e.g. [0001, 0007]
replaces: []      # RFC numbers this supersedes
capability_id: "cap.rfc.0000.<slug>.v1"
---

# RFC 0000: <Title>

## 0. Summary
One paragraph: what changes, who benefits, and what is the capability gate.

## 1. Motivation
- What pain point(s) exist today?
- Why is this not solved by existing Tasks/IR?
- Expected impact (DX, governance, safety, correctness, perf)?

## 2. Goals
- G1: ...
- G2: ...

## 3. Non-Goals
- NG1: ...
- NG2: ...

## 4. Background / Prior Art
- Relevant existing spec.md sections
- Comparable systems / approaches (brief)

## 5. Proposal (high level)
Describe the API surface / plan authoring changes at a high level.

## 6. Detailed Design

### 6.1 Surface API / Authoring
- New TS API(s) / node(s) / params
- Examples (minimal + realistic)
- Validation failures and error messages (what users see)

### 6.2 Plan / Fragment JSON changes
Specify exact JSON shapes.
- New fields
- Defaulting rules
- Backward/forward compatibility expectations

### 6.3 IR changes (ExprIR / PredIR / other)
- New IR node types (if any)
- Type rules
- Determinism rules
- SourceRef / sid behavior

### 6.4 Engine execution semantics
- Exact runtime behavior
- Failure modes (fail-closed vs best-effort)
- Concurrency model impact (if any)

### 6.5 Governance / registries impact
- keys.toml / params.toml / features.toml changes
- New lifecycle rules or enforcement points

### 6.6 Budgets / limits / complexity
- Time/cpu/mem budgets
- Worst-case bounds
- Plan complexity budget changes

### 6.7 Observability / Debuggability
- Trace fields added/changed
- Audit logs
- Error payload changes
- Visualizer impact

### 6.8 Caching impact
- Which digests must be included in cache key
- Whether schema_version bumps
- Whether capability sets need a digest

### 6.9 Security / sandbox / IO (if applicable)
- Trust model
- IO capabilities and allowlists
- Sandboxing model
- DoS considerations

## 7. Backward compatibility
- Additive vs breaking
- Migration plan
- Deprecation plan

## 8. Alternatives considered
List 2–4 serious alternatives and why they were rejected.

## 9. Risks and mitigations
- R1: ...
- Mitigation: ...

## 10. Test plan
- Unit tests
- Integration tests
- Golden files / fixtures
- Fault injection (if relevant)

## 11. Rollout plan
- Dev/test/prod gating
- Feature flags / capability enforcement
- Metrics to watch

## 12. Open questions
- Q1: ...
- Q2: ...

## Appendix A: Examples
Complete examples that should compile/run.

## Appendix B: Reference schemas
JSON schemas / type definitions.
