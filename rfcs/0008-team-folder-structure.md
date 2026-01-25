---
rfc: 0008
title: "Team-Based Plan Folder Structure"
status: Draft
created: 2026-01-25
updated: 2026-01-25
authors:
  - "Bob"
approvers: []
requires: []
replaces: []
capability_id: null  # No capability gate needed - tooling/organizational change
---

# RFC 0008: Team-Based Plan Folder Structure

## 0. Summary

Introduce a team-based folder structure for plans (`plans/<team>/<plan>.plan.ts`) with corresponding manifest, ownership, and artifact organization changes. Benefits: clear ownership, reduced merge conflicts, team-specific CI gates, and scalable multi-team development.

## 1. Motivation

- **Ownership ambiguity**: Flat `plans/` directory doesn't indicate who owns what
- **Merge conflicts**: Multiple teams editing `manifest.json` causes frequent conflicts
- **CI blast radius**: All plans rebuild on any change; no team-scoped validation
- **Discoverability**: Hard to find plans by domain/team in a flat list
- **Access control**: Can't apply team-specific CODEOWNERS rules

Expected impact:
- **DX**: Easier navigation, clear ownership, fewer conflicts
- **Governance**: Team-based review requirements via CODEOWNERS
- **CI**: Team-scoped builds and validation possible

## 2. Goals

- G1: Support `plans/<team>/<plan>.plan.ts` folder structure
- G2: Per-team manifests (`plans/<team>/manifest.json`) with optional root rollup
- G3: Artifact output mirrors source structure (`artifacts/plans/<team>/`)
- G4: CODEOWNERS integration for team-based review gates
- G5: Backward compatible - flat structure still works

## 3. Non-Goals

- NG1: Cross-team plan dependencies (use fragments for shared logic)
- NG2: Runtime team isolation (all plans run in same engine)
- NG3: Team-specific param/key registries (single global registry remains)
- NG4: Auto-discovery without manifest (explicit manifest required)

## 4. Background / Prior Art

- **Monorepo patterns**: Nx, Turborepo use workspace-based organization
- **Current state**: Flat `plans/` + single `manifest.json`
- **Fragment namespacing**: Already uses `<name>/<version>` pattern

## 5. Proposal (high level)

```
plans/
├── manifest.json          # Optional root manifest (can aggregate teams)
├── reels/
│   ├── manifest.json      # Team manifest
│   ├── OWNERS             # Team ownership file
│   ├── main.plan.ts
│   └── experiment_a.plan.ts
├── feed/
│   ├── manifest.json
│   ├── OWNERS
│   └── home_feed.plan.ts
└── search/
    ├── manifest.json
    ├── OWNERS
    └── query_ranking.plan.ts
```

Artifacts mirror structure:
```
artifacts/plans/
├── index.json             # Aggregated index
├── reels/
│   ├── index.json         # Team index
│   ├── main.plan.json
│   └── experiment_a.plan.json
└── feed/
    └── home_feed.plan.json
```

## 6. Detailed Design

### 6.1 Folder Structure Rules

**Team directory naming:**
- Lowercase alphanumeric + underscore: `[a-z][a-z0-9_]*`
- Max 32 characters
- Reserved names: `shared`, `common`, `_internal`

**Plan file naming:**
- Pattern: `<name>.plan.ts` (unchanged)
- Full plan name becomes: `<team>/<name>` (e.g., `reels/main`)

**Depth limit:**
- Max 1 level of nesting (no `plans/reels/experiments/v2/`)
- Flat within team directory

### 6.2 Manifest Changes

**Per-team manifest** (`plans/<team>/manifest.json`):
```json
{
  "schema_version": 1,
  "team": "reels",
  "owners": ["@reels-team"],
  "plans": [
    "main.plan.ts",
    "experiment_a.plan.ts"
  ]
}
```

**Root manifest** (`plans/manifest.json`) - optional aggregator:
```json
{
  "schema_version": 1,
  "mode": "teams",
  "teams": ["reels", "feed", "search"],
  "include_flat": []
}
```

Or legacy flat mode (backward compatible):
```json
{
  "schema_version": 1,
  "mode": "flat",
  "plans": [
    "plans/legacy_plan.plan.ts"
  ]
}
```

**Mode detection:**
1. If root manifest has `"mode": "teams"` → scan team directories
2. If root manifest has `"mode": "flat"` or `"plans"` array → legacy behavior
3. If no root manifest → auto-detect based on directory structure

### 6.3 Plan Naming Convention

**Qualified plan name**: `<team>/<plan_name>`
- Engine loads by qualified name: `--plan_name reels/main`
- HTTP API: `"plan": "reels/main"`
- Artifacts: `artifacts/plans/reels/main.plan.json`

**Backward compatibility:**
- Flat plans (no team prefix) still work
- Unqualified names search flat directory first, then error if ambiguous

### 6.4 Artifact Output Structure

```
artifacts/plans/
├── index.json                    # Global index
├── reels/
│   ├── index.json               # Team index
│   ├── main.plan.json
│   └── experiment_a.plan.json
└── legacy_plan.plan.json        # Flat plans at root
```

**Global index** (`artifacts/plans/index.json`):
```json
{
  "schema_version": 1,
  "generated_at": "2026-01-25T12:00:00Z",
  "teams": {
    "reels": {
      "plans": ["reels/main", "reels/experiment_a"],
      "index_path": "reels/index.json"
    }
  },
  "flat_plans": ["legacy_plan"],
  "all_plans": [
    "reels/main",
    "reels/experiment_a",
    "legacy_plan"
  ]
}
```

### 6.5 CODEOWNERS Integration

**Recommended CODEOWNERS pattern:**
```
# Team ownership
plans/reels/           @myorg/reels-team
plans/feed/            @myorg/feed-team
plans/search/          @myorg/search-team

# Registry changes require platform team
registry/              @myorg/platform-team
```

**OWNERS file** (optional, per-team):
```yaml
# plans/reels/OWNERS
owners:
  - "@alice"
  - "@bob"
  - "@myorg/reels-team"
approvers:
  - "@myorg/ranking-approvers"
```

### 6.6 Build Commands

```bash
# Build all teams
pnpm run plan:build:all

# Build specific team
pnpm run plan:build --team reels

# Build single plan (qualified name)
pnpm run dslc build plans/reels/main.plan.ts --out artifacts/plans/reels

# Sync manifests (discovers new plans)
pnpm run plan:manifest:sync              # All teams
pnpm run plan:manifest:sync --team reels # Single team
```

### 6.7 Engine Changes

**Plan loading with qualified names:**
```bash
# Load by qualified name
engine/bin/rankd --plan_name reels/main

# List plans shows qualified names
engine/bin/rankd --list-plans
# Output:
# reels/main
# reels/experiment_a
# feed/home_feed
# legacy_plan
```

**Plan resolution order:**
1. Exact match on qualified name
2. If unqualified, check flat plans
3. If unqualified and not found, error (no ambiguous search)

### 6.8 Migration Plan

**Phase 1: Add support (non-breaking)**
- Compiler accepts team directories
- Engine accepts qualified names
- Flat structure continues working

**Phase 2: Migrate existing plans**
- Move plans to team directories
- Update manifests
- Update any hardcoded plan names

**Phase 3: Deprecate flat structure (optional)**
- Warn on flat plans
- Require team prefix

## 7. Backward Compatibility

- **Fully additive**: Flat `plans/` structure continues working
- **No capability gate needed**: This is tooling/organizational, not IR change
- **Gradual migration**: Teams can migrate independently

## 8. Alternatives Considered

**A1: Prefix-based naming without folders**
```
plans/reels__main.plan.ts
plans/feed__home.plan.ts
```
Rejected: Less discoverable, no CODEOWNERS support, ugly.

**A2: Monorepo workspaces (separate packages)**
```
packages/plans-reels/
packages/plans-feed/
```
Rejected: Over-engineered, breaks current tooling, separate build configs.

**A3: Tags/metadata instead of folders**
```typescript
definePlan({
  name: "main",
  team: "reels",  // metadata
  ...
})
```
Rejected: Doesn't help with file organization, CODEOWNERS, or CI scoping.

## 9. Risks and Mitigations

- **R1**: Teams create inconsistent structures
  - Mitigation: Lint rules enforce naming conventions

- **R2**: Cross-team plan name collisions
  - Mitigation: Qualified names prevent collisions; `reels/main` ≠ `feed/main`

- **R3**: Migration disrupts existing workflows
  - Mitigation: Phased rollout, flat structure remains supported

## 10. Test Plan

- Unit tests: Manifest parsing with team structure
- Unit tests: Qualified name resolution
- Integration: Build all teams, verify artifact structure
- Golden files: Index generation with multiple teams
- CI: Verify CODEOWNERS patterns work

## 11. Rollout Plan

1. **Dev**: Implement compiler + engine support
2. **Test**: Create example team structure in `examples/plans/`
3. **Prod**: Migrate `plans/` to team structure incrementally
4. **Metrics**: Track build times per team, plan count per team

## 12. Open Questions

- Q1: Should team manifests support `extends` for shared config?
- Q2: Should we support team-specific param defaults/overrides?
- Q3: How to handle shared test fixtures across teams?
- Q4: Should `--list-plans` group by team in output?

## Appendix A: Examples

**Creating a new team:**
```bash
mkdir -p plans/discovery
cat > plans/discovery/manifest.json << 'EOF'
{
  "schema_version": 1,
  "team": "discovery",
  "owners": ["@discovery-team"],
  "plans": []
}
EOF

# Add first plan
cat > plans/discovery/explore.plan.ts << 'EOF'
import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "explore",
  params: {},
  build: (ctx) => {
    const v = ctx.viewer();
    const candidates = v.follow({ limit: 100 });
    candidates.take(50, { trace: "final" });
  },
});
EOF

# Sync manifest
pnpm run plan:manifest:sync --team discovery
```

**Querying team plans:**
```bash
# List all plans for a team
engine/bin/rankd --list-plans | grep "^discovery/"

# Run a team plan
echo '{"user_id": 1}' | engine/bin/rankd --plan_name discovery/explore
```

## Appendix B: Reference Schemas

**Team manifest schema:**
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "required": ["schema_version", "team", "plans"],
  "properties": {
    "schema_version": { "const": 1 },
    "team": {
      "type": "string",
      "pattern": "^[a-z][a-z0-9_]{0,31}$"
    },
    "owners": {
      "type": "array",
      "items": { "type": "string" }
    },
    "plans": {
      "type": "array",
      "items": { "type": "string", "pattern": "\\.plan\\.ts$" }
    }
  }
}
```

**Root manifest schema (teams mode):**
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "required": ["schema_version", "mode"],
  "properties": {
    "schema_version": { "const": 1 },
    "mode": { "enum": ["teams", "flat"] },
    "teams": {
      "type": "array",
      "items": { "type": "string" }
    },
    "include_flat": {
      "type": "array",
      "items": { "type": "string" }
    }
  }
}
```
