# Adding New Capabilities

This guide explains how to add a new capability to the dag-executor system, following RFC 0001.

## Overview

Capabilities are the extension mechanism for evolving the Plan/Fragment JSON IR without changing the base schema. Each capability:
- Has a unique, immutable ID following naming conventions
- Gates new features behind fail-closed validation
- May define an extension payload schema

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

### 2. Register the Capability in C++ Engine

Edit `engine/src/capability_registry.cpp`:

```cpp
// Add to supported_capabilities set
static const std::unordered_set<std::string> supported_capabilities = {
    "cap.rfc.0001.extensions_capabilities.v1",
    "cap.rfc.NNNN.<slug>.v1",  // <-- Add here
};
```

If your capability has a payload schema, add validation in `validate_capability_payload()`:

```cpp
void validate_capability_payload(std::string_view cap_id,
                                  const nlohmann::json &payload,
                                  std::string_view scope) {
  // ... existing validation ...

  // Add capability-specific validation
  if (cap_id == "cap.rfc.NNNN.<slug>.v1") {
    // Validate payload schema
    if (!payload.contains("required_field")) {
      throw std::runtime_error(
          std::string("capability '") + std::string(cap_id) + "' at " +
          std::string(scope) + ": missing required_field");
    }
  }
}
```

### 3. Add DSL Runtime Support (if needed)

If plans need to declare this capability, update `dsl/packages/runtime/src/plan.ts`:

```typescript
// In PlanCtx class
requireCapability(capId: string, payload?: Record<string, unknown>): void {
  // Implementation validates and stores capability
}
```

### 4. Add Artifact Validation (if needed)

If the capability introduces new artifact fields, update validation in:
- `dsl/packages/runtime/src/artifact-validation.ts` (shared validation)
- `engine/src/plan.cpp` (C++ parsing/validation)

### 5. Add Tests

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

### 6. Rebuild and Verify

```bash
# Rebuild engine
cmake --build engine/build --parallel

# Rebuild DSL
pnpm run build:dsl

# Run full CI
./scripts/ci.sh
```

## Extension Payload Schemas by RFC

### RFC 0001: Base Mechanism
- **Capability ID**: `cap.rfc.0001.extensions_capabilities.v1`
- **Payload**: Empty object `{}` required (no fields allowed)
- **Purpose**: Meta-capability that gates the extensions mechanism itself

### RFC 0002: Timeout Wrapper
- **Capability ID**: `cap.rfc.0002.timeout_wrapper.v1`
- **Payload Schema**:
```jsonc
{
  // Plan-level extension
  "cap.rfc.0002.timeout_wrapper.v1": {
    // No plan-level payload defined in v1
  }
}
```
- **Node-level extensions**: Defined per `timeout_wrapper` node params

### RFC 0003: Debug Capture & Postlude
- **Capability ID**: `cap.rfc.0003.debug_capture_postlude.v1`
- **Payload Schema**:
```jsonc
{
  "cap.rfc.0003.debug_capture_postlude.v1": {
    "postlude_jobs": [
      {
        "anchor_breakpoint": "plan::fragment::label",
        "job_fragment_digest": "sha256:...",
        "when": { /* optional predicate */ },
        "budgets": { /* job budgets */ },
        "sinks": ["sink_id"]
      }
    ]
  }
}
```

### RFC 0004: Runtime Request Branching
- **Capability ID**: `cap.rfc.0004.if_request_branching.v1`
- **Payload Schema**:
```jsonc
{
  "cap.rfc.0004.if_request_branching.v1": {
    // No plan-level payload defined in v1
  }
}
```
- **Node-level extensions**: Defined per `if_request` node params

### RFC 0005: Key Effects (writes_exact)
- **Capability ID**: `cap.rfc.0005.key_effects_writes_exact.v1`
- **Payload Schema**:
```jsonc
{
  "cap.rfc.0005.key_effects_writes_exact.v1": {
    // Effect metadata for tooling
    "node_effects": {
      "node_id": {
        "writes": ["key_id_1", "key_id_2"],
        "effect_type": "exact"  // or "may" or "unknown"
      }
    }
  }
}
```

### RFC 0006: Plan Visualizer
- **Capability ID**: `cap.rfc.0006.plan-visualizer.v1`
- **Payload Schema**: None (tooling-only, no runtime capability needed)
- **Note**: This is a pure tooling RFC; plans don't need to declare this capability

## File Locations Summary

| Component | Location |
|-----------|----------|
| C++ capability registry | `engine/src/capability_registry.cpp` |
| C++ capability header | `engine/include/capability_registry.h` |
| DSL runtime | `dsl/packages/runtime/src/plan.ts` |
| Artifact validation | `dsl/packages/runtime/src/artifact-validation.ts` |
| Plan parsing (C++) | `engine/src/plan.cpp` |
| Test fixtures (DSL) | `test/fixtures/plans/` |
| Test fixtures (Engine) | `artifacts/plans/bad_engine_*.plan.json` |
| CI tests | `scripts/ci.sh` |
| RFC docs | `rfcs/` |

## Governance Rules

1. **Fail-Closed**: Unknown capabilities cause plan rejection
2. **Immutable IDs**: Once published, capability IDs cannot change
3. **Breaking Changes**: Require new version (`v2`, `v3`, etc.)
4. **Extension Keys**: Must be declared in `capabilities_required`
5. **Payload Validation**: Strict schema validation per capability

## Digest Computation

The `capabilities_digest` is computed as:

```
sha256(canonical_json({
  "capabilities_required": [...],
  "extensions": {...}
}))
```

Where `canonical_json` sorts keys alphabetically and uses no whitespace.

Both TypeScript (`dsl/tools/build_all_plans.ts`) and C++ (`engine/src/capability_registry.cpp`) implementations must produce identical digests.

## Future: Capability Registry Codegen (Step 11.4)

Currently, capability validation is implemented separately in TypeScript and C++. A future improvement is to define capabilities in a TOML registry and codegen both implementations:

```toml
# registry/capabilities.toml (future)

[capabilities.rfc0001_extensions]
id = "cap.rfc.0001.extensions_capabilities.v1"
rfc = "0001"
status = "implemented"

[capabilities.rfc0001_extensions.plan_payload]
type = "object"
additional_properties = false  # empty {} required

[capabilities.rfc0002_timeout_wrapper]
id = "cap.rfc.0002.timeout_wrapper.v1"
rfc = "0002"
status = "draft"
requires = ["cap.rfc.0001.extensions_capabilities.v1"]

[capabilities.rfc0002_timeout_wrapper.node_payload]
type = "object"
properties.trace_timeout_events = { type = "boolean", optional = true }
```

Benefits:
- Single source of truth (no TS/C++ drift)
- Schema reviewable in one place
- Can generate documentation automatically
- Easier to add new capabilities
