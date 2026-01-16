# Adding New Capabilities

This guide explains how to add a new capability to the dag-executor system, following RFC 0001.

## Overview

Capabilities are the extension mechanism for evolving the Plan/Fragment JSON IR without changing the base schema. Each capability:
- Has a unique, immutable ID following naming conventions
- Gates new features behind fail-closed validation
- May define an extension payload schema (JSON Schema)
- Is defined in `registry/capabilities.toml` (single source of truth)

## Capability ID Naming Convention

Format: `cap.rfc.NNNN.<slug>.vK`

- `NNNN`: RFC number (zero-padded to 4 digits)
- `<slug>`: descriptive snake_case identifier
- `vK`: version number (starts at 1)

Examples:
- `cap.rfc.0001.extensions_capabilities.v1`
- `cap.rfc.0002.timeout_wrapper.v1`
- `cap.rfc.0003.debug_capture_postlude.v1`

## Step-by-Step Workflow

### 1. Write the RFC

Create `rfcs/NNNN-<name>.md` using `rfcs/0000-template.md`. Include:

```yaml
---
rfc: NNNN
capability_id: "cap.rfc.NNNN.<slug>.v1"
requires: [0001]  # All capabilities depend on RFC 0001
---
```

Define your extension payload schema in the RFC if needed.

### 2. Register the Capability in TOML

Edit `registry/capabilities.toml`:

```toml
[[capability]]
id = "cap.rfc.NNNN.<slug>.v1"
rfc = "NNNN"
name = "<slug>"
status = "draft"    # draft → implemented when ready
doc = "Description of what this capability enables"

# Optional: Define payload schema (JSON Schema)
# If omitted, no payload is allowed (must be null/absent)
payload_schema = '''
{
  "type": "object",
  "properties": {
    "trace_events": {"type": "boolean"}
  },
  "additionalProperties": false
}
'''
```

**Status values:**
- `implemented` - Fully supported by engine
- `draft` - RFC approved, not yet implemented
- `deprecated` - Will be removed in future version
- `blocked` - Rejected, plan using this will be rejected

**Payload schema:**
- Omit for capabilities that don't allow payloads
- JSON Schema subset: `type`, `properties`, `additionalProperties`, `required`
- Supported types: `object`, `boolean`, `string`, `number`

### 3. Run Codegen

```bash
# Regenerate TS + C++ from TOML
pnpm -C dsl run gen

# Verify generated files
pnpm -C dsl run gen:check
```

This generates:
- `dsl/packages/generated/capabilities.ts` - TS registry + validator
- `engine/include/capability_registry_gen.h` - C++ constexpr metadata
- `artifacts/capabilities.json` - JSON artifact
- `artifacts/capabilities.digest` - SHA256 digest

### 4. Add Engine-Specific Logic (if needed)

If your capability requires custom engine behavior beyond schema validation, update `engine/src/capability_registry.cpp`:

```cpp
void validate_capability_payload(std::string_view cap_id,
                                  const nlohmann::json &payload,
                                  std::string_view scope) {
  // ... schema-driven validation happens automatically ...

  // Add capability-specific semantic validation if needed
  if (cap_id == "cap.rfc.NNNN.<slug>.v1") {
    // Custom validation logic
  }
}
```

### 5. Add DSL Runtime Support (if needed)

If plans need to declare this capability, update `dsl/packages/runtime/src/plan.ts`:

```typescript
// In PlanCtx class
requireCapability(capId: string, payload?: Record<string, unknown>): void {
  // Implementation validates and stores capability
}
```

### 6. Add Tests

Create test fixtures:

```bash
# DSL negative test (dslc should reject)
test/fixtures/plans/bad_<feature>.plan.ts

# Engine negative test (engine should reject)
artifacts/plans/bad_engine_<feature>.plan.json

# Positive test
test/fixtures/plans/valid_<feature>.plan.ts
```

Add CI tests to `scripts/ci.sh`:

```bash
run_bg "Test NN: <Description>" bash -c '
# Test logic here
'
```

### 7. Rebuild and Verify

```bash
# Rebuild engine (picks up generated header)
cmake --build engine/build --parallel

# Rebuild DSL
pnpm run build:dsl

# Run full CI
./scripts/ci.sh
```

## File Locations Summary

| Component | Location |
|-----------|----------|
| Capability registry (TOML) | `registry/capabilities.toml` |
| Generated TS module | `dsl/packages/generated/capabilities.ts` |
| Generated C++ header | `engine/include/capability_registry_gen.h` |
| C++ runtime logic | `engine/src/capability_registry.cpp` |
| DSL runtime | `dsl/packages/runtime/src/plan.ts` |
| Artifact validation | `dsl/packages/runtime/src/artifact-validation.ts` |
| Test fixtures (DSL) | `test/fixtures/plans/` |
| Test fixtures (Engine) | `artifacts/plans/bad_engine_*.plan.json` |
| CI tests | `scripts/ci.sh` |

## Governance Rules

1. **Fail-Closed**: Unknown capabilities cause plan rejection
2. **Immutable IDs**: Once published, capability IDs cannot change
3. **Breaking Changes**: Require new version (`v2`, `v3`, etc.)
4. **Extension Keys**: Must be declared in `capabilities_required`
5. **Schema Validation**: Payloads validated against JSON Schema from registry
6. **Blocked Status**: Capabilities with `status = "blocked"` reject plans

## Digest Computation

Two digests are tracked:

### Capability Registry Digest

Computed from the canonical JSON of all capability entries:

```
capability_registry_digest = sha256(canonical_json({schema_version, entries}))
```

Available via:
- TS: `CAPABILITY_REGISTRY_DIGEST` from `@ranking-dsl/generated`
- C++: `kCapabilityRegistryDigest` from `capability_registry_gen.h`
- Engine: `engine/bin/rankd --print-registry`

### Plan Capabilities Digest

Computed per-plan from capabilities used:

```
capabilities_digest = sha256(canonical_json({
  "capabilities_required": [...],
  "extensions": {...}
}))
```

Where `canonical_json` sorts keys alphabetically and uses no whitespace.

Both TypeScript (`dsl/tools/build_all_plans.ts`) and C++ (`engine/src/capability_registry.cpp`) implementations produce identical digests.

## Example: Adding a Hypothetical RFC 0007

```toml
# registry/capabilities.toml

[[capability]]
id = "cap.rfc.0007.feature_caching.v1"
rfc = "0007"
name = "feature_caching"
status = "draft"
doc = "Enable feature caching with TTL and eviction policies"
payload_schema = '''
{
  "type": "object",
  "properties": {
    "default_ttl_seconds": {"type": "number"},
    "eviction_policy": {"type": "string"}
  },
  "required": ["default_ttl_seconds"],
  "additionalProperties": false
}
'''
```

After running `pnpm -C dsl run gen`:
- Plans can declare `capabilities_required: ["cap.rfc.0007.feature_caching.v1"]`
- Extensions must include `default_ttl_seconds` (required)
- Extensions may include `eviction_policy` (optional)
- No other fields allowed (`additionalProperties: false`)

## See Also

- [CAPABILITY_EXAMPLES.md](CAPABILITY_EXAMPLES.md) - Examples for each RFC capability
- [PLAN_AUTHORING_GUIDE.md](PLAN_AUTHORING_GUIDE.md) - How to write plans
- `registry/capabilities.toml` - Current capability definitions
