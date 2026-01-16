# step-12.2-writes-effect

## Findings

### Fixed
- ~~High (manifest parity)~~: **FIXED** - Changed `nlohmann::json::parse` to `nlohmann::ordered_json::parse` in `engine/src/writes_effect.cpp` to preserve key insertion order.
- ~~Medium (canonicalization)~~: **FIXED** - Added deduplication via `std::set` (C++) and `new Set()` (TS) in both evaluation and serialization. `Keys([1,1])` now produces `[1]`.

### Open
- Low (parity test gap): CI "writes_effect evaluator parity" only validates the TS-generated JSON shape (`scripts/ci.sh:559-583`); it never runs the C++ evaluator or compares results. Mitigated by comprehensive C++ unit tests (17 test cases, 46 assertions) that cover the same scenarios.

## Resolved Questions
- **Manifest digest ordering**: Yes, C++ now uses `ordered_json` end-to-end to match TS ordering.
- **Keys set semantics**: Yes, keys are deduped at eval/serialization time for canonical output.

## Design Notes
- **Source tasks use `Keys{}`**: This is intentional. `writes_effect` captures *param-dependent* writes. Source tasks have fixed schemas (their columns are part of the static contract), so they correctly declare no param-dependent writes. The existing `writes` field in TaskSpec handles static declarations.
