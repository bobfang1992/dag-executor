---
rfc: 0007
title: "Namespaced Task Operation Names"
status: Implemented
created: 2026-01-25
updated: 2026-01-25
authors:
  - "<name>"
approvers:
  - "<name>"
requires: []
replaces: []
capability_id: null  # Registry/governance change, no capability gate needed
---

# RFC 0007: Namespaced Task Operation Names

## 0. Summary

Introduce namespaced task operation names using `::` separator (e.g., `core::viewer`, `merchant::catalog`, `reels::trending`). Namespace is inferred from folder structure (`engine/src/tasks/<namespace>/<task>.cpp`). All tasks live in a namespace folder - shared primitives go in `core/`, test tasks in `test/`, and product-specific tasks in their vertical folder (`feed/`, `merchant/`, `reels/`, `ads/`).

## 1. Motivation

**Current state:**
- Task ops are flat identifiers: `viewer`, `follow`, `filter`, `vm`
- Works fine for a small set of core primitives
- No organizational structure as task count grows

**Problems emerging:**
- Different product teams (Feed, Reels, Merchant, Ads) will define domain-specific tasks
- Flat namespace risks collisions: `fetch_features` for feed vs merchant
- No clear ownership signal in task names
- Hard to discover tasks by domain in large registries

**Expected benefits:**
- Clear domain ownership: `merchant::catalog_lookup` vs `feed::user_prefs`
- Collision avoidance across verticals
- Better discoverability in tooling (filter by namespace)
- Cleaner audit logs and traces

## 2. Goals

- G1: All task ops use qualified names: `<namespace>::<op>`
- G2: Clear validation rules for namespace format
- G3: Tooling support (registry viewer, visualizer, CLI)
- G4: Migration path with alias window for existing plans

## 3. Non-Goals

- NG1: No explicit namespace field in TaskSpec / registration metadata. Namespace is inferred from repository folder structure to avoid mismatch/drift.
- NG2: Access control based on namespace (future RFC if needed)
- NG3: Namespace hierarchy (single level only: `ns::op`, not `ns::sub::op`)

## 4. Background / Prior Art

- **C++ namespaces**: `std::vector`, `boost::asio::io_context`
- **Rust crates**: `tokio::spawn`, `serde::Deserialize`
- **Protobuf packages**: `google.protobuf.Any`
- **gRPC services**: `grpc.health.v1.Health`

The `::` separator is familiar to C++/Rust developers and visually distinct from `.` (used in method chaining).

## 5. Proposal (high level)

Task operation names may optionally include a namespace prefix:

```
<namespace>::<op_name>
```

Where:
- `namespace`: lowercase alphanumeric + underscore, 2-32 chars
- `op_name`: lowercase alphanumeric + underscore, 1-64 chars

Examples:
- `core::viewer` - Shared viewer task
- `core::vm` - Shared expression evaluation
- `feed::user_prefs` - Feed user preferences
- `reels::trending_fetch` - Reels trending content
- `merchant::catalog_lookup` - Merchant catalog query
- `ads::bid_request` - Ads bidding task
- `test::sleep` - Test-only delay task

## 6. Detailed Design

### 6.1 Namespace Inference from Folder Structure

**Primary approach:** Infer namespace from directory structure. ALL tasks live in a namespace folder.

```
engine/src/tasks/
├── core/                     # Shared primitives
│   ├── vm.cpp                → op = "core::vm"
│   ├── filter.cpp            → op = "core::filter"
│   ├── sort.cpp              → op = "core::sort"
│   ├── take.cpp              → op = "core::take"
│   ├── concat.cpp            → op = "core::concat"
│   ├── viewer.cpp            → op = "core::viewer"
│   ├── follow.cpp            → op = "core::follow"
│   ├── media.cpp             → op = "core::media"
│   └── recommendation.cpp    → op = "core::recommendation"
├── test/                     # Test-only tasks
│   ├── sleep.cpp             → op = "test::sleep"
│   ├── fixed_source.cpp      → op = "test::fixed_source"
│   └── busy_cpu.cpp          → op = "test::busy_cpu"
├── internal/                 # Engine-internal tasks
│   └── ...
├── feed/                     # Feed vertical
│   └── user_prefs.cpp        → op = "feed::user_prefs"
├── merchant/                 # Merchant vertical
│   └── catalog_lookup.cpp    → op = "merchant::catalog_lookup"
└── reels/                    # Reels vertical
    └── trending_fetch.cpp    → op = "reels::trending_fetch"
```

**TaskSpec declares only the local name:**

```cpp
// engine/src/tasks/core/viewer.cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "viewer",  // Local name only; "core::" added by registry
    // ...
  };
}
```

**Build system computes the full qualified name:**

```cmake
# CMakeLists.txt - macro to register task with namespace
# Passes explicit relative path to avoid __FILE__ nondeterminism
register_task(core/viewer.cpp)      # → core::viewer
register_task(merchant/catalog.cpp) # → merchant::catalog
```

**Implementation (avoiding `__FILE__` nondeterminism):**

Raw `__FILE__` is problematic:
- Absolute paths vary across machines/build directories
- Path separators differ (`/` vs `\`)

Preferred approach: CMake passes explicit relative path as a compile definition:

```cmake
function(register_task TASK_PATH)
  # Extract namespace from path: "core/viewer.cpp" → "core"
  get_filename_component(TASK_DIR ${TASK_PATH} DIRECTORY)
  get_filename_component(TASK_NAME ${TASK_PATH} NAME_WE)

  target_compile_definitions(tasks PRIVATE
    TASK_NAMESPACE="${TASK_DIR}"
    TASK_LOCAL_OP="${TASK_NAME}"
  )
endfunction()
```

```cpp
// In task registration macro
#define REGISTER_TASK(TaskClass) \
  static TaskRegistrar<TaskClass> registrar(TASK_NAMESPACE, TASK_LOCAL_OP)
```

**Rule**: The derived qualified op must be identical across machines and build directories.

**Benefits:**
- Single source of truth (folder = namespace)
- No mismatch possible between declared op and folder
- Enforces organizational discipline
- Familiar pattern (Go packages, Java packages)
- ALL tasks have a namespace (no special cases)

**Reserved namespaces:**
- `core` - Shared primitives (vm, filter, viewer, follow, etc.)
- `test` - Test-only tasks (sleep, fixed_source, busy_cpu)
- `internal` - Engine-internal tasks

### 6.2 Op Name Format

**Qualified name grammar (namespace required):**
```
qualified_op := namespace "::" local_op
namespace    := [a-z][a-z0-9_]{1,31}
local_op     := [a-z][a-z0-9_]{0,63}
```

**Validation regex:**
```
^([a-z][a-z0-9_]{1,31})::([a-z][a-z0-9_]{0,63})$
```

**Validation tests:**
| Input | Result |
|-------|--------|
| `core::vm` | ✅ valid |
| `reels::viewer` | ✅ valid |
| `Core::vm` | ❌ reject (uppercase) |
| `core::VM` | ❌ reject (uppercase) |
| `a::b::c` | ❌ reject (multi-level) |
| `ns::-x` | ❌ reject (invalid char) |
| `::op` | ❌ reject (empty namespace) |
| `ns::` | ❌ reject (empty op) |
| `vm` | ❌ reject (unqualified) - or alias during migration |

### 6.3 TaskSpec Changes

```cpp
struct TaskSpec {
  std::string op;  // Local name (e.g., "viewer")
  // Full qualified name computed by registry: "feed::viewer"
  // ... existing fields ...
};
```

TaskRegistry computes full name at registration time based on source path.

### 6.4 Ownership via CODEOWNERS

Namespace ownership enforced via GitHub CODEOWNERS:

```
# .github/CODEOWNERS
engine/src/tasks/core/       @infra-team
engine/src/tasks/test/       @infra-team
engine/src/tasks/internal/   @infra-team
engine/src/tasks/feed/       @feed-team
engine/src/tasks/merchant/   @merchant-team
engine/src/tasks/reels/      @reels-team
engine/src/tasks/ads/        @ads-team
```

This provides:
- PR approval required from owning team
- Clear audit trail for namespace changes
- No runtime enforcement needed

### 6.3 Plan JSON

No change to plan JSON structure. Node `op` field already accepts strings:

```json
{
  "node_id": "n1",
  "op": "merchant::catalog_lookup",
  "params": { "endpoint": "ep_0003" }
}
```

### 6.4 Registry (tasks.toml)

Tasks organized by namespace in comments/sections:

```toml
# === Core Tasks ===
[[task]]
op = "vm"
# ...

[[task]]
op = "filter"
# ...

# === Feed Tasks ===
[[task]]
op = "feed::viewer"
# ...

[[task]]
op = "feed::follow"
# ...

# === Merchant Tasks ===
[[task]]
op = "merchant::catalog_lookup"
# ...
```

### 6.5 Generated TypeScript

**Root-level shorthand is `core::` only** (prevents API sprawl):

```typescript
// core:: tasks available at root level (compile-time shorthand)
ctx.viewer(opts)            // → serializes to "core::viewer" in plan JSON
cs.vm(opts)                 // → serializes to "core::vm"
cs.filter(opts)             // → serializes to "core::filter"

// Also accessible via explicit namespace
ctx.core.viewer(opts)       // → "core::viewer"
cs.core.vm(opts)            // → "core::vm"

// Other namespaces MUST use explicit prefix (no root shorthand)
cs.merchant.catalog(opts)   // → "merchant::catalog"
cs.feed.userPrefs(opts)     // → "feed::user_prefs"
cs.reels.trending(opts)     // → "reels::trending_fetch"
```

**Key rule**: TS shorthands are compile-time ergonomics only. Plan JSON always contains qualified `ns::op` names.

Implementation: Generated `task-impl.ts`:

```typescript
// Core tasks exported at root AND under namespace
export const viewer = (ctx: PlanCtx, opts: ViewerOpts) => {
  return addNode({ op: "core::viewer", ... });  // Always qualified
};
export const vm = (cs: CandidateSet, opts: VmOpts) => {
  return addNode({ op: "core::vm", ... });
};

export const core = { viewer, vm, filter, ... };

export const merchant = {
  catalog: (cs: CandidateSet, opts: CatalogOpts) => {
    return addNode({ op: "merchant::catalog", ... });
  },
};
```

### 6.6 Engine Execution

No change to execution semantics. Task lookup by full op string:

```cpp
const auto& spec = TaskRegistry::get("merchant::catalog_lookup");
```

### 6.7 Observability

Traces and audit logs include full namespaced op:

```json
{
  "node_id": "n42",
  "op": "merchant::catalog_lookup",
  "latency_us": 1234
}
```

### 6.8 Visualizer

Node labels show full op name. Color coding by namespace (optional enhancement):

| Namespace | Color |
|-----------|-------|
| (core/none) | Gray |
| feed | Blue |
| reels | Purple |
| merchant | Green |
| ads | Orange |

### 6.9 CLI

```bash
# List tasks, optionally filter by namespace
engine/bin/rankd --list-tasks
engine/bin/rankd --list-tasks --namespace feed

# Print manifest shows namespace
engine/bin/rankd --print-task-manifest
```

## 7. Backward Compatibility

### Canonical Naming

- All manifests, tracing, audit logs, and caches use **qualified operation names** `ns::op`
- The engine treats `ns::op` as the canonical identifier
- Plan JSON must serialize op names as `ns::op`

### Migration / Alias Window

During a migration window, the engine accepts unqualified op names as aliases:

| Input | Resolves To | Behavior |
|-------|-------------|----------|
| `vm` | `core::vm` | Emit deprecation warning (once per process) |
| `core::vm` | `core::vm` | No warning (canonical) |

**End state**: After the migration window, aliases are removed; plans must use qualified names.

### Migration Path

1. **Move files**: `engine/src/tasks/vm.cpp` → `engine/src/tasks/core/vm.cpp`
2. **Alias window**: Engine accepts both `vm` and `core::vm`, warns on unqualified
3. **Update plans**: `"op": "vm"` → `"op": "core::vm"`
4. **Remove aliases**: Engine rejects unqualified ops

### DSL Ergonomics (TypeScript Only)

Root-level shorthand is a **compile-time convenience for `core::` only**:

```typescript
cs.vm(opts)       // TS shorthand, serializes to "core::vm" in plan JSON
cs.core.vm(opts)  // explicit, also serializes to "core::vm"

// Other namespaces require explicit prefix (no root shorthand)
cs.merchant.catalog(opts)  // serializes to "merchant::catalog"
```

This prevents API sprawl where every team wants root-level access.

## 8. Alternatives Considered

### A1: Explicit namespace in op string

```cpp
.op = "merchant::catalog_lookup"  // Declare full name explicitly
```

Rejected: Duplicates folder structure; allows mismatches; more typing.

### A2: Dot separator (`feed.viewer`)

Rejected: Conflicts with method chaining in DSL (`cs.viewer().follow()`). Would require `cs["feed.viewer"]()` syntax.

### A3: Slash separator (`feed/viewer`)

Rejected: Looks like file paths; confusing in logs/traces.

### A4: Prefix convention (`feed_viewer`)

Rejected: No structural separation; harder to parse programmatically; doesn't support tooling (filter by namespace).

### A5: Hierarchical namespaces (`feed::user::viewer`)

Rejected for now: Single level is sufficient. Can extend later if needed.

## 9. Risks and Mitigations

**R1: Namespace proliferation**
- Mitigation: Document recommended namespaces; require RFC for new top-level namespaces

**R2: Long op names in logs**
- Mitigation: 32+64 char limit; tooling can abbreviate (`m::catalog` in compact views)

**R3: Build system complexity**
- Mitigation: CMake macro encapsulates all path handling; tasks just use `REGISTER_TASK(Class)`

## 9.1 Governance Benefits

Namespaced ops transform "stringly-typed flat identifiers" into "organizationally meaningful stable IDs", enabling:

| Benefit | How |
|---------|-----|
| **Ownership** | CODEOWNERS aligns with namespace folders |
| **Discovery** | Tooling can list/filter by namespace |
| **Governance/Audit** | Traces are readable and unambiguous |
| **Stability policies** | Future: per-namespace graduation/deprecation rules |

## 10. Test Plan

### 10.1 Name Validation Tests

| Input | Expected |
|-------|----------|
| `core::vm` | ✅ accept |
| `reels::viewer` | ✅ accept |
| `Core::vm` | ❌ reject (uppercase namespace) |
| `core::VM` | ❌ reject (uppercase op) |
| `a::b::c` | ❌ reject (multi-level) |
| `ns::-x` | ❌ reject (invalid char) |
| `::op` | ❌ reject (empty namespace) |
| `ns::` | ❌ reject (empty op) |

### 10.2 Folder → Op Inference Determinism

- Task in `engine/src/tasks/core/viewer.cpp` registers as `core::viewer`
- Moving file to different namespace folder changes the op (expected)
- Behavior is stable across platforms (path separators normalized)
- Derived op identical across machines and build directories

### 10.3 Alias Window (Migration Period)

- `vm` accepted and resolves to `core::vm`
- Manifest/tracing prints canonical `core::vm`
- Deprecation warning emitted once per process (not spam)
- After window: `vm` rejected with clear error

### 10.4 Tooling Support

- `rankd --list-tasks` shows qualified names
- `rankd --list-tasks --namespace core` filters correctly
- `--print-task-manifest` outputs qualified names
- Manifest export groups by namespace

### 10.5 Integration Tests

- Register namespaced task, execute in plan
- Golden files: Plan JSON with namespaced ops
- Traces include qualified op names

## 11. Rollout Plan

1. **Phase 1**: Infrastructure
   - Update CMakeLists.txt to support `tasks/<namespace>/` subdirectories
   - Update TaskRegistrar to infer namespace from `__FILE__`
   - Update `--print-task-manifest` to output qualified names

2. **Phase 2**: Migrate core tasks
   - Move `engine/src/tasks/*.cpp` → `engine/src/tasks/core/*.cpp`
   - Verify all tests pass with `core::` prefix
   - Update DSL codegen to emit `core::` in plans

3. **Phase 3**: Alias period (optional)
   - Engine accepts both `vm` and `core::vm` during transition
   - Emit deprecation warnings for unnnamespaced ops
   - Update all plans to use `core::` prefix

4. **Phase 4**: Remove aliases
   - Engine rejects unnnamespaced ops
   - Clean up alias code

5. **Phase 5**: Add vertical namespaces as needed
   - Create `feed/`, `merchant/`, `reels/` folders
   - Teams add domain-specific tasks

## 12. Open Questions

- Q1: Do we need a namespace registry (like keys/params) or just folder conventions + CODEOWNERS?
- Q2: How long should the alias migration window be? (e.g., 1 release cycle, 30 days)
- Q3: Should `--print-task-manifest` output include source file path for verification?

## Appendix A: Examples

### A.1 Namespaced Task Implementation

```cpp
// engine/src/tasks/merchant/catalog_lookup.cpp
// Full op name "merchant::catalog_lookup" inferred from CMake-provided path
namespace rankd {

class CatalogLookup {
public:
  static TaskSpec spec() {
    return TaskSpec{
      .op = "catalog_lookup",  // Local name only; "merchant::" added by registry
      .params_schema = {
        {.name = "endpoint", .type = TaskParamType::EndpointRef, .required = true},
        {.name = "sku_key", .type = TaskParamType::Int, .required = true},
      },
      .writes = {5001, 5002},  // merchant_title, merchant_price
      .output_pattern = OutputPattern::UnaryPreserveView,
      .is_io = true,
    };
  }
  // ...
};

// REGISTER_TASK macro uses CMake-provided TASK_NAMESPACE and TASK_LOCAL_OP
// Avoids __FILE__ nondeterminism across build environments
REGISTER_TASK(CatalogLookup);

} // namespace rankd
```

### A.2 Plan Using Namespaced Tasks

```typescript
import { definePlan, EP } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'merchant_ranking',
  build: (ctx) => {
    // core:: tasks available at root for ergonomics
    const source = ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 50 });

    // Merchant-specific task
    const enriched = source.merchant.catalog({
      endpoint: EP.redis.merchant_cache,
      sku_key: Key.product_id,
    });

    // core:: tasks
    const scored = enriched.vm({
      outKey: Key.final_score,
      expr: Key.relevance * coalesce(P.price_weight, 0.3),
    });

    return scored.take({ count: 20 });
  },
});
```

### A.3 Plan JSON Output

```json
{
  "name": "merchant_ranking",
  "nodes": [
    {
      "node_id": "n0",
      "op": "core::viewer",
      "params": { "endpoint": "ep_0001" }
    },
    {
      "node_id": "n1",
      "op": "core::follow",
      "inputs": ["n0"],
      "params": { "endpoint": "ep_0001", "fanout": 50 }
    },
    {
      "node_id": "n2",
      "op": "merchant::catalog",
      "inputs": ["n1"],
      "params": { "endpoint": "ep_0003", "sku_key": 4001 }
    },
    {
      "node_id": "n3",
      "op": "core::vm",
      "inputs": ["n2"],
      "params": { "out_key": 3005, "expr_id": "e0" }
    },
    {
      "node_id": "n4",
      "op": "core::take",
      "inputs": ["n3"],
      "params": { "count": 20 }
    }
  ]
}
```
