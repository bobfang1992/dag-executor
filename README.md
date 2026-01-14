# Ranking DSL + Engine

A governed, type-safe DSL for building ranking pipelines.

## Quick Start

### Build

```bash
# Full build: install deps, codegen, build DSL + compiler, compile plans
pnpm install
pnpm run build

# Or step by step:
pnpm -C dsl install              # Install DSL dependencies
pnpm run gen                     # Regenerate registry tokens
pnpm run build:dsl               # Build TypeScript packages (including dslc)
pnpm run plan:build:all          # Compile all plans with QuickJS sandbox
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel
```

### Run

```bash
# Step 00 fallback (no plan)
echo '{"request_id": "test-123"}' | engine/bin/rankd

# Execute a plan
echo '{"request_id": "test"}' | engine/bin/rankd --plan artifacts/plans/demo.plan.json

# Print registry digests
engine/bin/rankd --print-registry
```

### CI

```bash
./scripts/ci.sh
```

### DSL Commands

```bash
pnpm -C dsl run lint       # Lint TypeScript
pnpm -C dsl run gen        # Regenerate all outputs from registry TOML
pnpm -C dsl run gen:check  # Verify generated outputs are up-to-date
```

## Authoring Plans

Plans are TypeScript files that compile to JSON artifacts using the QuickJS-based `dslc` compiler. The compiler bundles your plan with the DSL runtime and executes it in a secure sandbox (no eval, no Node.js globals, no dynamic imports).

### Basic Structure

```typescript
// examples/plans/my_plan.plan.ts
import { definePlan, E, Pred, Key, P } from "@ranking-dsl/runtime";

export default definePlan({
  name: "my_plan",
  build: (ctx) => {
    return ctx.viewer
      .follow({ fanout: 100 })
      .filter({ pred: Pred.cmp(">", E.key(Key.model_score_1), E.const(0.5)) })
      .take({ count: 10 });
  },
});
```

### Source Tasks

```typescript
ctx.viewer.follow({ fanout: 100, trace: "source" })
ctx.viewer.fetch_cached_recommendation({ fanout: 50 })
```

### Transform Tasks

```typescript
.vm({ outKey: Key.final_score, expr: E.mul(E.key(Key.id), E.const(0.1)) })
.filter({ pred: Pred.cmp(">=", E.key(Key.final_score), E.const(0.5)) })
.take({ count: 10 })
set1.concat(set2)
```

### Expression Builder (E)

```typescript
E.const(42)                      // literal number
E.constNull()                    // null
E.key(Key.model_score_1)         // column ref
E.param(P.media_age_penalty)     // param ref
E.add(a, b)  E.sub(a, b)  E.mul(a, b)  E.neg(a)
E.coalesce(a, b)                 // null fallback
```

### Predicate Builder (Pred)

```typescript
Pred.cmp("==", a, b)             // also !=, <, <=, >, >=
Pred.in(E.key(Key.country), ["US", "CA"])  // homogeneous list
Pred.isNull(expr)  Pred.notNull(expr)
Pred.regex(Key.title, "pattern", "i")      // regex on string col
Pred.and(p1, p2)  Pred.or(p1, p2)  Pred.not(p)
```

### Compile & Run

```bash
# Compile all plans (QuickJS sandbox)
pnpm run plan:build:all

# Compile a single plan
pnpm run dslc build examples/plans/my_plan.plan.ts --out artifacts/plans

# Compile all plans (manifest-based, QuickJS backend - CI default)
pnpm run plan:build:all

# Run with engine
echo '{"request_id": "test"}' | engine/bin/rankd --plan artifacts/plans/my_plan.plan.json

# With param overrides
echo '{"request_id": "t", "param_overrides": {"media_age_penalty_weight": 0.5}}' \
  | engine/bin/rankd --plan artifacts/plans/my_plan.plan.json
```

### Alternative: Node-based Compiler (Debug/Fallback)

```bash
# Single plan with Node compiler (faster iteration, less secure)
pnpm run plan:build:node examples/plans/my_plan.plan.ts --out artifacts/plans

# All plans with Node backend
pnpm run plan:build:all:node
```

**Note:** Edit `examples/plans/manifest.json` to add/remove plans from batch compilation.

### Security & Compiler Backends

**QuickJS-based compiler (dslc)** - Primary, used in CI:
- ❌ No `eval()` or `Function` constructor
- ❌ No `process`, `require`, `module`, or Node.js globals
- ❌ No file system, network, or dynamic imports
- ✅ Deterministic builds (same input → same output)
- ✅ Portable (WASM-based, no native addons)

**Node-based compiler (compiler-node)** - Legacy fallback for debugging:
- ⚠️ Full Node.js access (less secure)
- ✅ Faster iteration during development
- ✅ Incremental builds (skip up-to-date plans)

**See [Plan Compiler Guide](docs/PLAN_COMPILER_GUIDE.md) for detailed usage.**

## Project Structure

```
engine/                  # C++23 execution engine
  src/                   # Source files
  include/               # Headers
  bin/                   # Built binaries (rankd)
registry/                # Key/Param/Feature definitions (TOML)
dsl/packages/
  runtime/               # Plan authoring API (E, Pred, definePlan)
  compiler/              # dslc compiler (QuickJS-based, primary)
  compiler-node/         # Legacy Node-based compiler (fallback)
  generated/             # Generated Key/Param/Feature tokens
artifacts/plans/         # Compiled plan JSON files
examples/plans/          # Example .plan.ts files
scripts/                 # CI and tooling scripts
```

## Code Style

C++ code follows LLVM style (see `.clang-format`). Run `clang-format -i` on source files before committing.
