# step-12.2-writes-effect

## Findings

### Fixed
- ~~High (manifest parity)~~: **FIXED** - Changed `nlohmann::json::parse` to `nlohmann::ordered_json::parse` in `engine/src/writes_effect.cpp` to preserve key insertion order.
- ~~Medium (canonicalization)~~: **FIXED** - Added deduplication via `std::set` (C++) and `new Set()` (TS) in both evaluation and serialization. `Keys([1,1])` now produces `[1]`.
- ~~P2 (source tasks)~~: **FIXED** - Source tasks now declare emitted keys in `.writes` field instead of using `EffectKeys{}`. See Design v2 below.

### Open
- Low (parity test gap): CI "writes_effect evaluator parity" only validates the TS-generated JSON shape (`scripts/ci.sh:559-583`); it never runs the C++ evaluator or compares results. Mitigated by comprehensive C++ unit tests (17 test cases, 46 assertions) that cover the same scenarios.

## Design v2: Writes Contract

The original design had confusion around source tasks using `EffectKeys{}` which reports "no writes" even though they emit columns. This was addressed with a unified "Writes Contract" model:

### Key Changes

1. **Unified Writes Contract**: `UNION(writes, writes_effect)`
   - `writes`: Fixed/static keys (always written)
   - `writes_effect`: Dynamic/param-dependent keys (optional)
   - System computes the union automatically

2. **Source Tasks**: Now declare emitted keys in `.writes`
   - `viewer.follow`: `.writes = {KeyId::country, KeyId::title}`
   - `viewer.fetch_cached_recommendation`: `.writes = {KeyId::country}`

3. **No-Write Tasks**: Omit `writes_effect` entirely
   - `filter`, `take`, `concat`: `.writes = {}`, no `writes_effect`

4. **Param-Dependent Tasks**: Use `writes_effect` only
   - `vm`: `.writes = {}`, `.writes_effect = EffectFromParam{"out_key"}`

5. **Helper Function**: `compute_effective_writes(spec)` combines both fields

### Benefits
- Most tasks fill only ONE field
- Source tasks properly declare their schema
- No more confusing `EffectKeys{}` for tasks that do emit columns
- Codex P2 finding addressed

## Resolved Questions
- **Manifest digest ordering**: Yes, C++ now uses `ordered_json` end-to-end to match TS ordering.
- **Keys set semantics**: Yes, keys are deduped at eval/serialization time for canonical output.
- **Source task writes**: Addressed via Design v2 - fixed schema keys go in `.writes`.

## Design Notes
- **Writes Contract semantics**: The effective writes for a task is the union of static `.writes` and dynamic `.writes_effect`. Task authors typically fill only one.
- **`EffectKeys{}` usage**: Now mainly for internal composition (in Union/SwitchEnum cases). Authors should prefer `.writes` for fixed keys.
