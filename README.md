# Ranking DSL + Engine

A governed, type-safe DSL for building ranking pipelines.

## Quick Start

### Build

```bash
# Install DSL dependencies and build engine
pnpm -C dsl install
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

## Project Structure

```
engine/              # C++23 execution engine
  src/               # Source files
  include/           # Headers (including generated keys.h, params.h, features.h)
  bin/               # Built binaries (rankd)
registry/            # Key/Param/Feature definitions (TOML)
  keys.toml          # Key registry (8 keys)
  params.toml        # Param registry (3 params)
  features.toml      # Feature registry (2 features)
dsl/                 # TypeScript DSL tooling
  src/codegen.ts     # Codegen tool (TOML -> TS/C++/JSON)
  packages/generated # Generated TS tokens (Key, P, Feat)
artifacts/           # Compiled artifacts
  plans/             # Plan JSON files
  *.json             # Registry JSON with schema_version
  *.digest           # SHA-256 digests
scripts/             # CI and tooling scripts
```

## Code Style

C++ code follows LLVM style (see `.clang-format`). Run `clang-format -i` on source files before committing.
