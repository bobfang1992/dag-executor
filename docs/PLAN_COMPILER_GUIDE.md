# Plan Compiler Invocation Guide

This document describes how to compile ranking plans with the two available compilers.

## Overview

Two compilers are available:
- **QuickJS-based (dslc)**: Primary compiler, used in CI and production
- **Node-based (compiler-node)**: Legacy fallback for debugging

**CI Policy:** CI must use QuickJS only. The manifest-based build system (`plan:build:all`) uses QuickJS by default.

---

## QuickJS-based Compiler (dslc) - Primary/Default

### Single Plan Compilation

```bash
# Recommended: via npm script
pnpm run dslc build <plan.ts> --out <output-dir>

# Example:
pnpm run dslc build examples/plans/my_plan.plan.ts --out artifacts/plans
```

### Compile All Plans (Manifest-based)

```bash
# Default: uses QuickJS backend
pnpm run plan:build:all

# Reads from: examples/plans/manifest.json
# Outputs to: artifacts/plans/
```

The manifest file (`examples/plans/manifest.json`) lists all plans to compile:
```json
{
  "schema_version": 1,
  "plans": [
    "examples/plans/reels_plan_a.plan.ts",
    "examples/plans/concat_plan.plan.ts",
    "examples/plans/regex_plan.plan.ts"
  ]
}
```

**To add a new plan:** Add its path to the `plans` array in the manifest.

### Advanced/Debug: Direct Invocation

```bash
# Direct node invocation (not recommended for normal use)
node dsl/packages/compiler/dist/cli.js build <plan.ts> --out <output-dir>

# Advanced: custom manifest and output
tsx dsl/tools/build_all_plans.ts --manifest custom.json --out custom/dir --backend quickjs
```

---

## Node-based Compiler (compiler-node) - Legacy/Fallback

**Use for:** Debugging, development iteration, or when QuickJS compilation fails.

### Single Plan Compilation

```bash
# Recommended: via npm script with explicit --out
pnpm run plan:build:node <plan.ts> --out artifacts/plans

# Example:
pnpm run plan:build:node examples/plans/my_plan.plan.ts --out artifacts/plans
```

### Compile All Plans (Manifest-based, Node backend)

```bash
# Uses Node backend instead of QuickJS
pnpm run plan:build:all:node

# Or with custom options:
tsx dsl/tools/build_all_plans.ts --backend node
```

### Advanced Options

```bash
# With incremental build tracking (skip up-to-date plans)
pnpm run plan:build:node examples/plans/my_plan.plan.ts --out artifacts/plans

# Force rebuild (ignore timestamps)
pnpm run plan:build:node examples/plans/my_plan.plan.ts --out artifacts/plans --force

# Direct tsx invocation
tsx dsl/packages/compiler-node/src/cli.ts <plan.ts> --out artifacts/plans
```

---

## Quick Reference

| Compiler | Primary Command | Use Case |
|----------|----------------|----------|
| **QuickJS (dslc)** | `pnpm run dslc build <file> --out <dir>` | Production, CI, default |
| **Node (compiler-node)** | `pnpm run plan:build:node <file> --out <dir>` | Debugging, fallback |
| **Build all (QuickJS)** | `pnpm run plan:build:all` | CI, production builds |
| **Build all (Node)** | `pnpm run plan:build:all:node` | Debug builds |

### Key Differences

| Feature | QuickJS (dslc) | Node (compiler-node) |
|---------|----------------|---------------------|
| **Execution** | Sandboxed (no eval, no I/O) | Full Node.js access |
| **Security** | ✅ Locked down | ⚠️ Less restricted |
| **Determinism** | ✅ Guaranteed | ✅ With stable imports |
| **Speed** | Moderate (bundling + sandbox) | Fast (direct execution) |
| **Incremental** | ❌ No | ✅ Yes (with --force) |
| **CI Use** | ✅ Required | ❌ Not for CI |

---

## Metadata

Both compilers add `built_by` metadata to generated artifacts:

```json
{
  "built_by": {
    "backend": "quickjs",  // or "node"
    "tool": "dslc",        // or "compiler-node"
    "tool_version": "0.1.0",
    "bundle_digest": "a1b2c3d4..."  // QuickJS only
  }
}
```

This metadata is informational only and does not affect execution.

---

## Troubleshooting

### QuickJS compilation fails
- Try Node compiler: `pnpm run plan:build:node <plan> --out artifacts/plans`
- Check for use of Node globals (process, fs, require) in your plan
- Check for dynamic imports or eval

### Incremental builds not working
- Node compiler only: use `--force` to rebuild all
- QuickJS always rebuilds (no incremental support)

### Manifest not updating
- Edit `examples/plans/manifest.json` directly
- No need to update package.json scripts
