# Plan Authoring Guide

This guide walks you through creating, building, and running a new plan.

## Quick Start

```bash
# 1. Create your plan file (name must match plan_name inside)
touch plans/my_plan.plan.ts

# 2. Write your plan (see template below)

# 3. Add to manifest
pnpm run plan:manifest:sync

# 4. Build all plans
pnpm run plan:build:all

# 5. Run your plan
echo '{"request_id": "test"}' | engine/bin/rankd --plan_name my_plan
```

## Step-by-Step

### 1. Create the Plan File

Plans live in one of two directories:

| Directory | Purpose | Build Command |
|-----------|---------|---------------|
| `plans/` | Official plans (CI, production) | `pnpm run plan:build:all` |
| `examples/plans/` | Examples and tutorials | `pnpm run plan:build:examples` |

**Important:** The filename must match the `name` in `definePlan()`:
- File: `plans/my_plan.plan.ts`
- Name: `definePlan({ name: "my_plan", ... })`

### 2. Write Your Plan

Here's a minimal template:

```typescript
import { definePlan } from "@ranking-dsl/runtime";
import { Key, P } from "@ranking-dsl/generated";

export default definePlan({
  name: "my_plan", // Must match filename (my_plan.plan.ts)
  build: (ctx) => {
    // 1. Get candidates from a source
    const candidates = ctx.viewer.follow({ fanout: 100 });

    // 2. Transform (optional): compute scores, filter, etc.
    const scored = candidates.vm({
      key: Key.final_score,
      expr: Key.id.mul(P.media_age_penalty_weight),
    });

    // 3. Return final candidates with output keys
    return scored.take({
      count: 10,
      outputKeys: [Key.id, Key.final_score],
    });
  },
});
```

### 3. Update the Manifest

The manifest is the source of truth for which plans to build. After creating your plan:

```bash
# For plans in plans/
pnpm run plan:manifest:sync

# For plans in examples/plans/
pnpm run plan:manifest:sync:examples
```

This scans the directory and updates `manifest.json`.

### 4. Build Your Plan

```bash
# Build all official plans
pnpm run plan:build:all

# Build all example plans
pnpm run plan:build:examples

# Build a single plan (for quick iteration)
pnpm run dslc build plans/my_plan.plan.ts --out artifacts/plans
```

Output: `artifacts/plans/my_plan.plan.json`

### 5. Run Your Plan

```bash
# Run with default request
echo '{"request_id": "test"}' | engine/bin/rankd --plan_name my_plan

# Run with param overrides
echo '{"request_id": "test", "param_overrides": {"media_age_penalty_weight": 0.5}}' \
  | engine/bin/rankd --plan_name my_plan

# List available plans
engine/bin/rankd --list-plans
```

## Common Operations

### Filter Candidates

```typescript
import { Pred } from "@ranking-dsl/runtime";

const filtered = candidates.filter({
  pred: Pred.cmp(Key.final_score, ">=", 0.5),
});

// String filtering
const usOnly = candidates.filter({
  pred: Key.country.in(["US", "CA"]),
});

// Regex filtering
const matching = candidates.filter({
  pred: Pred.regex(Key.title, "^Breaking"),
});
```

### Compute Scores (vm)

```typescript
import { E } from "@ranking-dsl/runtime";

const scored = candidates.vm({
  key: Key.final_score,
  expr: E.add(
    E.key(Key.model_score_1),
    E.mul(E.key(Key.model_score_2), E.param(P.boost_weight))
  ),
});

// Or using operator overloads
const scored2 = candidates.vm({
  key: Key.final_score,
  expr: Key.model_score_1 + Key.model_score_2 * P.boost_weight,
});
```

### Concatenate Sources

```typescript
const source1 = ctx.viewer.follow({ fanout: 50 });
const source2 = ctx.viewer.fetch_cached_recommendation({ fanout: 50 });

const combined = source1.concat(source2);
```

### Use Parameters

Parameters are defined in `registry/params.toml`. Use them in expressions:

```typescript
// In vm expressions
Key.id.mul(P.media_age_penalty_weight)

// In filter predicates
Pred.cmp(Key.score, ">=", P.esr_cutoff)
```

Override at runtime:
```json
{
  "request_id": "test",
  "param_overrides": {
    "media_age_penalty_weight": 0.3,
    "esr_cutoff": 1.5
  }
}
```

## Troubleshooting

### "Plan name doesn't match filename"

The compiler enforces that `plan_name` matches the filename:

```
Error: Plan name "wrong_name" doesn't match filename "my_plan.plan.ts".
Rename file to "wrong_name.plan.ts" or change plan name to "my_plan".
```

### "Undefined is not allowed"

All DSL inputs must be defined. Check for missing parameters:

```typescript
// Bad: count might be undefined
candidates.take({ count: maybeUndefined })

// Good: provide a default
candidates.take({ count: value ?? 10 })
```

### Plan not found at runtime

1. Check the plan was built: `ls artifacts/plans/my_plan.plan.json`
2. Check it's in the index: `cat artifacts/plans/index.json | grep my_plan`
3. Rebuild if needed: `pnpm run plan:build:all`

## Workflow Summary

```
┌─────────────────────────────────────────────────────────────┐
│  1. Create: plans/my_plan.plan.ts                           │
│                                                             │
│  2. Sync:   pnpm run plan:manifest:sync                     │
│                                                             │
│  3. Build:  pnpm run plan:build:all                         │
│             → artifacts/plans/my_plan.plan.json             │
│             → artifacts/plans/index.json (updated)          │
│                                                             │
│  4. Run:    echo '{"request_id":"x"}' | rankd --plan_name   │
└─────────────────────────────────────────────────────────────┘
```

## Reference

- [PLAN_COMPILER_GUIDE.md](PLAN_COMPILER_GUIDE.md) - Compiler internals
- [CLAUDE.md](../CLAUDE.md) - Full project documentation
- `registry/keys.toml` - Available keys
- `registry/params.toml` - Available parameters
