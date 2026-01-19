# Plan Compiler Invocation Guide

This document describes how to compile ranking plans and manage the plan store.

## Plan Store Overview

The project uses a central plan store model:

| Directory | Purpose | Compiled To |
|-----------|---------|-------------|
| `plans/` | Official plans (CI, production) | `artifacts/plans/` |
| `examples/plans/` | Example/tutorial plans | `artifacts/plans-examples/` |

Each store has:
- `manifest.json` - List of plans to compile (committed SSOT)
- `index.json` - Generated index with plan names and digests

---

## Compiler

The `dslc` compiler uses QuickJS for secure, sandboxed compilation:
- No `eval()` or `Function` constructor
- No Node.js globals (process, require, module)
- No file system, network, or dynamic imports
- Deterministic builds (same input → same output)

---

## Building Official Plans (Recommended)

### Build All Official Plans

```bash
# Build all official plans (uses plans/manifest.json)
pnpm run plan:build:all

# Outputs to: artifacts/plans/
# Generates: artifacts/plans/index.json
```

### Build Single Plan

```bash
# Single plan with dslc
pnpm run dslc build plans/my_plan.plan.ts --out artifacts/plans
```

### Run with Engine

```bash
# Run by plan name (recommended)
echo '{}' | engine/bin/rankd --plan_name reels_plan_a

# Run by explicit path
echo '{}' | engine/bin/rankd --plan artifacts/plans/reels_plan_a.plan.json

# List available plans
engine/bin/rankd --list-plans
```

---

## Building Example Plans

```bash
# Build all example plans
pnpm run plan:build:examples

# Outputs to: artifacts/plans-examples/
# Generates: artifacts/plans-examples/index.json

# Run example plan
echo '{}' | engine/bin/rankd --plan_dir artifacts/plans-examples --plan_name reels_plan_a
```

---

## Managing Manifests

Manifests list plans to compile. They are committed files (SSOT).

### Sync Manifests

```bash
# Sync official manifest (scans plans/**/*.plan.ts)
pnpm run plan:manifest:sync

# Sync examples manifest (scans examples/plans/**/*.plan.ts)
pnpm run plan:manifest:sync:examples
```

### Manifest Format

```json
{
  "schema_version": 1,
  "plans": [
    "plans/concat_plan.plan.ts",
    "plans/reels_plan_a.plan.ts",
    "plans/regex_plan.plan.ts"
  ]
}
```

Plans are sorted lexicographically for determinism.

---

## Advanced: Direct Invocation

```bash
# Direct dslc invocation
node dsl/packages/compiler/dist/cli.js build <plan.ts> --out <output-dir>

# Custom manifest and output
tsx dsl/tools/build_all_plans.ts --manifest custom.json --out custom/dir
```

---

## Quick Reference

| Command | Description |
|---------|-------------|
| `pnpm run plan:build:all` | Build official plans |
| `pnpm run plan:build:examples` | Build example plans |
| `pnpm run plan:manifest:sync` | Sync official manifest |
| `pnpm run dslc build <file> --out <dir>` | Build single plan |

---

## Engine Plan Loading

| Option | Description |
|--------|-------------|
| `--plan <path>` | Load plan from explicit path |
| `--plan_name <name>` | Load plan by name from plan_dir |
| `--plan_dir <dir>` | Plan store directory (default: `artifacts/plans`) |
| `--list-plans` | List available plans from index.json |

**Security:** Plan names must match `[A-Za-z0-9_]+` only. Invalid names are rejected.

---

## Index File Format

Generated `index.json`:

```json
{
  "schema_version": 1,
  "plans": [
    {
      "name": "reels_plan_a",
      "path": "reels_plan_a.plan.json",
      "digest": "sha256:...",
      "capabilities_digest": "",
      "built_by": {
        "backend": "quickjs",
        "tool": "dslc",
        "tool_version": "0.1.0"
      }
    }
  ]
}
```

**Fields:**
- `digest`: SHA256 of the plan artifact
- `capabilities_digest`: SHA256 of `{capabilities_required, extensions}` (empty string if no capabilities)

---

## Troubleshooting

### Compilation fails
- Check for use of Node globals (process, fs, require) in your plan
- Check for dynamic imports or eval
- Ensure natural expressions use `Key`, `P`, `coalesce` directly (not aliased)

### Manifest out of sync
- Run `pnpm run plan:manifest:sync` to regenerate from directory scan
- Commit the updated manifest

### Plan not found by name
- Check `--plan_dir` points to correct directory
- Run `engine/bin/rankd --list-plans` to see available plans
- Ensure plan was compiled (check `index.json`)
