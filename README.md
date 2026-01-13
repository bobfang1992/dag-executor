# Ranking DSL + Engine

A governed, type-safe DSL for building ranking pipelines.

## Quick Start

### Build

```bash
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel
```

### Run

```bash
echo '{"request_id": "test-123"}' | engine/bin/rankd
```

### CI

```bash
./scripts/ci.sh
```

## Project Structure

```
engine/          # C++23 execution engine
  src/           # Source files
  bin/           # Built binaries (rankd)
registry/        # Key/Param/Feature definitions (TOML)
artifacts/       # Compiled plan/fragment JSON
dsl/             # TypeScript DSL (not yet implemented)
scripts/         # CI and tooling scripts
```
