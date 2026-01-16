# Capability Examples by RFC

This document shows the TOML registry definition and JSON artifact examples for each capability.

---

## RFC 0001: Base Extensions/Capabilities Mechanism

The foundational capability that enables all other capabilities.

### TOML Registry Definition

```toml
[[capability]]
id = "cap.rfc.0001.extensions_capabilities.v1"
rfc = "0001"
name = "extensions_capabilities"
status = "implemented"
doc = "Base extensions/capabilities mechanism for IR evolution"
payload_schema = '''
{
  "type": "object",
  "additionalProperties": false
}
'''
```

### Plan Artifact Example

```json
{
  "schema_version": 1,
  "plan_name": "my_plan",
  "capabilities_required": [
    "cap.rfc.0001.extensions_capabilities.v1"
  ],
  "extensions": {
    "cap.rfc.0001.extensions_capabilities.v1": {}
  },
  "nodes": [...],
  "outputs": [...]
}
```

**Notes:**
- Payload must be empty object `{}` (no fields allowed)
- This capability is the meta-capability enabling the extensions mechanism itself

---

## RFC 0002: Timeout Wrapper

Region deadline with fallback execution.

### TOML Registry Definition (Hypothetical)

```toml
[[capability]]
id = "cap.rfc.0002.timeout_wrapper.v1"
rfc = "0002"
name = "timeout_wrapper"
status = "draft"
doc = "Timeout wrapper with region deadline and fallback"
payload_schema = '''
{
  "type": "object",
  "properties": {
    "trace_timeout_events": {"type": "boolean"}
  },
  "additionalProperties": false
}
'''
```

### Plan Artifact Example

```json
{
  "schema_version": 1,
  "plan_name": "timeout_plan",
  "capabilities_required": [
    "cap.rfc.0001.extensions_capabilities.v1",
    "cap.rfc.0002.timeout_wrapper.v1"
  ],
  "extensions": {
    "cap.rfc.0001.extensions_capabilities.v1": {},
    "cap.rfc.0002.timeout_wrapper.v1": {
      "trace_timeout_events": true
    }
  },
  "nodes": [
    {
      "node_id": "timeout_region",
      "op": "timeout_wrapper",
      "inputs": ["source"],
      "params": {
        "deadline_ms": 100,
        "fallback": "fallback_source"
      },
      "extensions": {
        "cap.rfc.0002.timeout_wrapper.v1": {
          "trace_timeout_events": true
        }
      }
    }
  ],
  "outputs": ["timeout_region"]
}
```

---

## RFC 0003: Debug Capture & Postlude

Breakpoints, capture, and postlude jobs for debugging.

### TOML Registry Definition (Hypothetical)

```toml
[[capability]]
id = "cap.rfc.0003.debug_capture_postlude.v1"
rfc = "0003"
name = "debug_capture_postlude"
status = "draft"
doc = "Breakpoints, capture, and postlude jobs for debugging"
payload_schema = '''
{
  "type": "object",
  "properties": {
    "postlude_jobs": {
      "type": "array"
    },
    "breakpoints": {
      "type": "array"
    }
  },
  "additionalProperties": false
}
'''
```

### Plan Artifact Example

```json
{
  "schema_version": 1,
  "plan_name": "debug_plan",
  "capabilities_required": [
    "cap.rfc.0001.extensions_capabilities.v1",
    "cap.rfc.0003.debug_capture_postlude.v1"
  ],
  "extensions": {
    "cap.rfc.0001.extensions_capabilities.v1": {},
    "cap.rfc.0003.debug_capture_postlude.v1": {
      "postlude_jobs": [
        {
          "anchor_breakpoint": "plan::main::after_filter",
          "job_fragment_digest": "sha256:abc123...",
          "when": null,
          "budgets": {"max_nodes": 10},
          "sinks": ["debug_sink"]
        }
      ],
      "breakpoints": ["plan::main::after_filter"]
    }
  },
  "nodes": [
    {
      "node_id": "filter_node",
      "op": "filter",
      "inputs": ["source"],
      "params": {"pred_id": "p1"},
      "extensions": {
        "cap.rfc.0003.debug_capture_postlude.v1": {
          "breakpoint": "plan::main::after_filter"
        }
      }
    }
  ],
  "outputs": ["filter_node"]
}
```

---

## RFC 0004: Runtime Request Branching

Runtime branching based on request fields.

### TOML Registry Definition (Hypothetical)

```toml
[[capability]]
id = "cap.rfc.0004.if_request_branching.v1"
rfc = "0004"
name = "if_request_branching"
status = "draft"
doc = "Runtime request branching based on request fields"
# No plan-level payload, node-level only
```

### Plan Artifact Example

```json
{
  "schema_version": 1,
  "plan_name": "branching_plan",
  "capabilities_required": [
    "cap.rfc.0001.extensions_capabilities.v1",
    "cap.rfc.0004.if_request_branching.v1"
  ],
  "extensions": {
    "cap.rfc.0001.extensions_capabilities.v1": {}
  },
  "nodes": [
    {
      "node_id": "branch",
      "op": "if_request",
      "inputs": ["source_a", "source_b"],
      "params": {
        "condition": {"field": "experiment_group", "equals": "treatment"},
        "then_input": 0,
        "else_input": 1
      },
      "extensions": {
        "cap.rfc.0004.if_request_branching.v1": {
          "trace_branch_taken": true
        }
      }
    }
  ],
  "outputs": ["branch"]
}
```

---

## RFC 0005: Key Effects (writes_exact)

Compile-time effect inference for key writes.

> **✅ Design Resolution (2026-01-16)**
>
> RFC 0005 now separates two concerns:
>
> **A) Core language metadata (no capability needed)**
> - `key_effects` field in Plan/Fragment artifacts (like `expr_table`, `pred_table`)
> - Effect inference: `Exact(K) | May(K) | Unknown`
> - Always emitted by compiler, universally available for tooling
>
> **B) Capability-gated strict enforcement**
> - `cap.rfc.0005.key_effects_writes_exact.v1` gates strict validation
> - Required for meta-tasks with branching (`timeout_wrapper`, `if_request`)
> - Enforces `writes_exact(branch_a) == writes_exact(branch_b)`
> - Compiler auto-adds capability when lowering strict-shape constructs
>
> This design ensures effect metadata is always present (core) while strict enforcement remains opt-in (capability). Plans cannot bypass validation—if they use strict-shape meta-tasks, the compiler forces the capability requirement.

### TOML Registry Definition (Hypothetical)

```toml
[[capability]]
id = "cap.rfc.0005.key_effects_writes_exact.v1"
rfc = "0005"
name = "key_effects_writes_exact"
status = "draft"
doc = "Compile-time effect inference for key writes"
payload_schema = '''
{
  "type": "object",
  "properties": {
    "node_effects": {"type": "object"}
  },
  "additionalProperties": false
}
'''
```

### Plan Artifact Example

```json
{
  "schema_version": 1,
  "plan_name": "effects_plan",
  "capabilities_required": [
    "cap.rfc.0001.extensions_capabilities.v1",
    "cap.rfc.0005.key_effects_writes_exact.v1"
  ],
  "extensions": {
    "cap.rfc.0001.extensions_capabilities.v1": {},
    "cap.rfc.0005.key_effects_writes_exact.v1": {
      "node_effects": {
        "vm_score": {
          "writes": ["final_score"],
          "effect_type": "exact"
        },
        "vm_boost": {
          "writes": ["boost_score", "final_score"],
          "effect_type": "exact"
        }
      }
    }
  },
  "nodes": [
    {
      "node_id": "vm_score",
      "op": "vm",
      "inputs": ["source"],
      "params": {"key_id": 4, "expr_id": "e1"}
    },
    {
      "node_id": "vm_boost",
      "op": "vm",
      "inputs": ["vm_score"],
      "params": {"key_id": 5, "expr_id": "e2"}
    }
  ],
  "outputs": ["vm_boost"]
}
```

---

## RFC 0006: Plan Visualizer

Tooling-only capability for plan visualization (no runtime behavior).

### TOML Registry Definition (Hypothetical)

```toml
[[capability]]
id = "cap.rfc.0006.plan-visualizer.v1"
rfc = "0006"
name = "plan_visualizer"
status = "draft"
doc = "Plan visualizer tool (no runtime capability needed)"
# No payload - this is a tooling-only RFC
```

**Notes:**
- This is a pure tooling RFC
- Plans don't need to declare this capability
- No runtime behavior or validation

---

## Summary: Payload Schema Patterns

### No Payload Allowed

```toml
# Omit payload_schema entirely, or:
# payload_schema is not defined
```

Plans cannot include this capability in `extensions`.

### Empty Object Only

```toml
payload_schema = '''
{
  "type": "object",
  "additionalProperties": false
}
'''
```

Plans must use `{}` as the extension payload.

### Optional Properties

```toml
payload_schema = '''
{
  "type": "object",
  "properties": {
    "trace_events": {"type": "boolean"},
    "debug_mode": {"type": "string"}
  },
  "additionalProperties": false
}
'''
```

Plans can include any subset of defined properties:
- `{}`
- `{"trace_events": true}`
- `{"debug_mode": "verbose"}`
- `{"trace_events": false, "debug_mode": "quiet"}`

### Required Properties

```toml
payload_schema = '''
{
  "type": "object",
  "properties": {
    "timeout_ms": {"type": "number"},
    "fallback_mode": {"type": "string"}
  },
  "required": ["timeout_ms"],
  "additionalProperties": false
}
'''
```

Plans must include `timeout_ms`:
- `{"timeout_ms": 100}` - valid
- `{"timeout_ms": 100, "fallback_mode": "skip"}` - valid
- `{}` - **invalid** (missing required)
- `{"fallback_mode": "skip"}` - **invalid** (missing required)

---

## DSL Usage Example

```typescript
import { definePlan } from "@ranking-dsl/runtime";
import { Key } from "@ranking-dsl/generated";

export default definePlan({
  name: "example_with_capabilities",
  build: (ctx) => {
    // Declare capabilities with optional payload
    ctx.requireCapability("cap.audit", { level: "verbose" });
    ctx.requireCapability("cap.debug", { trace: true });

    const source = ctx.viewer.follow({
      fanout: 100,
      // Node-level extension
      extensions: {
        "cap.debug": { node_debug: true }
      }
    });

    return source.take({
      count: 10,
      outputKeys: [Key.id],
    });
  },
});
```

Resulting artifact:

```json
{
  "capabilities_required": ["cap.audit", "cap.debug"],
  "extensions": {
    "cap.audit": {"level": "verbose"},
    "cap.debug": {"trace": true}
  },
  "nodes": [{
    "node_id": "viewer_follow_0",
    "extensions": {
      "cap.debug": {"node_debug": true}
    },
    ...
  }]
}
```
