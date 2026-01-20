# Task Implementation Guide (Infra Engineers)

This guide explains how to create new tasks in the dag-executor engine.

## Overview

Tasks are the building blocks of ranking pipelines. Infra engineers implement tasks in C++; ranking engineers compose them via the TypeScript DSL.

**Key Principles:**
- Tasks declare their contracts (params, reads, writes) via `TaskSpec`
- Auto-registration via `TaskRegistrar<T>` template
- Fail-closed validation: unknown params/types rejected
- Columnar data model: `RowSet` with `ColumnBatch` (SoA)

## Task Class Structure

Each task is a class with two static methods:

```cpp
// engine/src/tasks/my_task.cpp
#include "task_registry.h"

namespace rankd {

class MyTask {
public:
  // 1. Declare the task contract
  static TaskSpec spec();

  // 2. Implement execution logic
  static RowSet run(const std::vector<RowSet>& inputs,
                    const ValidatedParams& params,
                    const ExecCtx& ctx);
};

// 3. Auto-register at load time
static TaskRegistrar<MyTask> registrar;

} // namespace rankd
```

## TaskSpec Fields

```cpp
struct TaskSpec {
  std::string op;                          // Unique operation name
  std::vector<ParamField> params_schema;   // Parameter definitions
  std::vector<KeyId> reads;                // Static key reads (audit)
  std::vector<KeyId> writes;               // Static/fixed key writes
  DefaultBudget default_budget;            // Timeout (MVP: ignored)
  OutputPattern output_pattern;            // Output shape contract (rows)
  std::optional<WritesEffectExpr> writes_effect; // Dynamic/param-dependent writes
};
```

### Operation Name (`op`)

- Unique identifier: `"vm"`, `"filter"`, `"viewer.follow"`
- Namespaced with `.` for related ops: `"viewer.follow"`, `"viewer.fetch_cached_recommendation"`

### Parameter Schema (`params_schema`)

```cpp
struct ParamField {
  std::string name;                        // Param name in JSON
  TaskParamType type;                      // Type (see below)
  bool required;                           // Must be present?
  bool nullable = false;                   // Allow null value?
  std::optional<ParamDefaultValue> default_value;
};
```

**Available Types:**

| Type | JSON | C++ Access |
|------|------|------------|
| `TaskParamType::Int` | number (int) | `params.get_int("name")` |
| `TaskParamType::Float` | number (float) | `params.get_float("name")` |
| `TaskParamType::Bool` | boolean | `params.get_bool("name")` |
| `TaskParamType::String` | string | `params.get_string("name")` |
| `TaskParamType::ExprId` | string | Reference to `expr_table` entry |
| `TaskParamType::PredId` | string | Reference to `pred_table` entry |

**Common Pattern - trace param:**
```cpp
{.name = "trace", .type = TaskParamType::String, .required = false, .nullable = true}
```

### Output Pattern (`output_pattern`)

Declares the **row shape** contract for task output:

| Pattern | Description | Example Tasks |
|---------|-------------|---------------|
| `SourceFanoutDense` | Source task, returns `fanout` rows | `viewer.follow` |
| `UnaryPreserveView` | 1 input, same rows, may add columns | `vm` |
| `UnarySubsetView` | 1 input, subset of rows | `filter` |
| `PrefixOfInput` | 1 input, first N rows | `take` |
| `BinaryConcat` | 2 inputs, concatenated | `concat` |

### Default Budget

```cpp
.default_budget = {.timeout_ms = 50}
```

Currently ignored by executor (MVP), but declared for future enforcement.

---

## Writes Contract (RFC 0005)

The **Writes Contract** declares which columns a task may materialize or overwrite. It consists of two fields that work together:

```
Writes Contract = UNION( Keys(writes), writes_effect )
```

| Field | Purpose | When to Use |
|-------|---------|-------------|
| `writes` | Fixed/static keys | Task always writes the same columns |
| `writes_effect` | Dynamic/param-dependent keys | Columns depend on params |

**Key insight:** Most tasks only need to fill ONE of these fields.

### Why Two Fields?

- **`writes`** is the preferred way to declare fixed keys (simple, explicit)
- **`writes_effect`** is used only when keys depend on params (e.g., `out_key`)

The system computes the union automatically. You do NOT need to manually combine them.

### Decision Flowchart

```
Does the task materialize/overwrite any columns?
│
├─ No → .writes = {}, omit .writes_effect
│       (e.g., filter, take, sort)
│
└─ Yes → Are the written keys constant for all param values?
    │
    ├─ Yes (fixed schema) → put keys in .writes, omit .writes_effect
    │                       (e.g., source tasks)
    │
    └─ No (param-dependent) → How are keys determined?
        │
        ├─ Single param (like out_key) → .writes_effect = EffectFromParam{...}
        │
        ├─ Enum switch (like stage) → .writes_effect = EffectSwitchEnum{...}
        │
        └─ Multiple dynamic sources → .writes_effect = EffectUnion{...}

Mixed (fixed + dynamic)?
  → Put fixed keys in .writes
  → Put dynamic part in .writes_effect
  → System computes the union automatically
```

### Quick Reference

| Task Type | `writes` | `writes_effect` |
|-----------|----------|-----------------|
| Source (fixed schema) | `{3001, 3002}` | `std::nullopt` |
| Filter/Take (no writes) | `{}` | `std::nullopt` |
| vm (param-dependent) | `{}` | `EffectFromParam{"out_key"}` |
| fetch_features (mixed) | `{static_keys}` | `EffectSwitchEnum{...}` |

### Dynamic Writes Expressions

For param-dependent writes, use one of these `writes_effect` forms:

#### `EffectFromParam{"param_name"}` - Single Key from Param

Use when: Task writes to **one key specified by a param**.

```cpp
// vm writes to whatever out_key param specifies
.writes_effect = EffectFromParam{"out_key"}
```

#### `EffectSwitchEnum{"param", cases}` - Enum-Dependent

Use when: Output keys depend on an **enum param** (like stage).

```cpp
// fetch_features writes different keys per stage
std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
cases["esr"] = makeEffectKeys({4001, 4002});
cases["lsr"] = makeEffectKeys({4003, 4004});

.writes_effect = EffectSwitchEnum{"stage", std::move(cases)}
```

#### `EffectUnion{items}` - Multiple Dynamic Sources

Use when: Multiple param-dependent write sources need to be **combined**.

```cpp
// Task with multiple dynamic outputs
std::vector<std::shared_ptr<WritesEffectExpr>> items;
items.push_back(makeEffectFromParam("out_key"));
items.push_back(makeEffectSwitchEnum("stage", cases));

.writes_effect = EffectUnion{std::move(items)}
```

#### `EffectKeys{...}` - For Internal Use

`EffectKeys` is mainly used internally (in `EffectUnion` or `EffectSwitchEnum` cases).
For fixed keys, prefer putting them in `.writes` instead.

### Evaluation Results

When the system evaluates the writes contract with param bindings (gamma):

| Result | Meaning |
|--------|---------|
| `Exact(keys)` | Will write exactly these keys |
| `May(keys)` | Might write any subset of these keys |
| `Unknown` | Cannot determine at link time |

---

## Implementation Workflow

### Step 1: Create the Task File

```bash
touch engine/src/tasks/my_task.cpp
```

### Step 2: Implement TaskSpec

```cpp
#include "task_registry.h"

namespace rankd {

class MyTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
      .op = "my_task",
      .params_schema = {
        {.name = "input_param", .type = TaskParamType::Int, .required = true},
        {.name = "trace", .type = TaskParamType::String, .required = false, .nullable = true},
      },
      .reads = {},
      .writes = {},  // Set if task writes fixed columns
      .default_budget = {.timeout_ms = 50},
      .output_pattern = OutputPattern::UnaryPreserveView,
      // .writes_effect - only set if writes depend on params
    };
  }
  // ...
};

} // namespace rankd
```

### Step 3: Implement run()

```cpp
static RowSet run(const std::vector<RowSet>& inputs,
                  const ValidatedParams& params,
                  const ExecCtx& ctx) {
  // 1. Validate input count
  if (inputs.size() != 1) {
    throw std::runtime_error("my_task: expected exactly 1 input");
  }

  // 2. Extract validated params
  int64_t input_param = params.get_int("input_param");

  // 3. Validate param values
  if (input_param <= 0) {
    throw std::runtime_error("my_task: 'input_param' must be > 0");
  }

  // 4. Process input
  const auto& input = inputs[0];

  // 5. Return result (share batch when possible)
  return input; // or input.withBatch(new_batch)
}
```

### Step 4: Add Auto-Registration

```cpp
// At bottom of file, inside namespace rankd
static TaskRegistrar<MyTask> registrar;
```

### Step 5: Add to CMakeLists.txt

```cmake
# engine/CMakeLists.txt
add_library(tasks STATIC
  src/tasks/vm.cpp
  src/tasks/filter.cpp
  src/tasks/take.cpp
  src/tasks/my_task.cpp  # ADD THIS
)
```

### Step 6: Add Unit Tests

```cpp
// engine/tests/test_my_task.cpp
#include <catch2/catch_test_macros.hpp>
#include "task_registry.h"

TEST_CASE("MyTask validates params", "[my_task]") {
  // Test validation logic
}

TEST_CASE("MyTask processes input correctly", "[my_task]") {
  // Test execution logic
}
```

### Step 7: Add DSL Bindings (Optional)

If ranking engineers need a DSL surface:

```typescript
// dsl/packages/runtime/src/candidate-set.ts
myTask(opts: { inputParam: number; trace?: string }): CandidateSet {
  // Add node to plan graph
}
```

### Step 8: Rebuild and Test

```bash
# Rebuild engine
cmake --build engine/build --parallel

# Run unit tests
engine/bin/rowset_tests

# Run CI
./scripts/ci.sh
```

---

## Common Patterns

### Source Task (Fixed Schema)

Source tasks emit a fixed set of columns. Declare them in `.writes`.

```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "my_source",
    .params_schema = {
      {.name = "fanout", .type = TaskParamType::Int, .required = true},
    },
    .writes = {3001, 3002},  // country, title - the emitted schema
    .output_pattern = OutputPattern::SourceFanoutDense,
    // writes_effect omitted - no param-dependent writes
  };
}

static RowSet run(const std::vector<RowSet>& inputs, ...) {
  if (!inputs.empty()) {
    throw std::runtime_error("my_source: expected 0 inputs");
  }
  // Create and return new ColumnBatch with country, title columns
}
```

### Transform Task (Param-Dependent Output)

Tasks like `vm` write to a column specified by a param.

```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "my_transform",
    .params_schema = {
      {.name = "out_key", .type = TaskParamType::Int, .required = true},
      {.name = "expr_id", .type = TaskParamType::ExprId, .required = true},
    },
    .writes = {},  // No fixed writes
    .output_pattern = OutputPattern::UnaryPreserveView,
    .writes_effect = EffectFromParam{"out_key"},  // Writes depend on param
  };
}

static RowSet run(const std::vector<RowSet>& inputs, ...) {
  // Add column specified by out_key, return input.withBatch(new_batch)
}
```

### Filter Task (No Column Writes)

Tasks that only affect rows (not columns) have no writes.

```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "my_filter",
    .params_schema = {
      {.name = "pred_id", .type = TaskParamType::PredId, .required = true},
    },
    .writes = {},  // No column writes
    .output_pattern = OutputPattern::UnarySubsetView,
    // writes_effect omitted - no writes at all
  };
}

static RowSet run(const std::vector<RowSet>& inputs, ...) {
  // Update selection, return input.withSelection(new_sel)
}
```

---

## File Locations

| Component | Location |
|-----------|----------|
| Task implementations | `engine/src/tasks/*.cpp` |
| TaskSpec/TaskRegistry | `engine/include/task_registry.h` |
| Task manifest (generated) | `registry/tasks.toml` |
| Generated TS types | `dsl/packages/generated/tasks.ts` |
| Generated TS impls | `dsl/packages/generated/task-impl.ts` |
| writes_effect types | `engine/include/writes_effect.h` |
| RowSet/ColumnBatch | `engine/include/rowset.h`, `column_batch.h` |
| Key registry | `engine/include/key_registry.h` |
| Param registry | `engine/include/param_registry.h` |
| Unit tests | `engine/tests/test_*.cpp` |
| Plan API wrappers | `dsl/packages/runtime/src/plan.ts` |

---

## Checklist

- [ ] Create `engine/src/tasks/<name>.cpp`
- [ ] Implement `spec()` with all required fields
- [ ] Implement `run()` with proper validation
- [ ] Add `TaskRegistrar<T>` at bottom
- [ ] Add to `CMakeLists.txt`
- [ ] Declare writes contract:
  - Fixed columns → `.writes = {key_ids}`
  - Param-dependent → `.writes_effect = ...`
  - No writes → both empty/omitted
- [ ] Choose correct `output_pattern` (row shape)
- [ ] Add unit tests
- [ ] Regenerate TypeScript (types + impls auto-generated):
  ```bash
  cmake --build engine/build --parallel
  engine/bin/rankd --print-task-manifest > registry/tasks.toml
  pnpm run gen
  ```
- [ ] Run `./scripts/ci.sh`

---

## See Also

- [PLAN_AUTHORING_GUIDE.md](PLAN_AUTHORING_GUIDE.md) - For ranking engineers
- [ADDING_CAPABILITIES.md](ADDING_CAPABILITIES.md) - For IR extensions
- `engine/include/task_registry.h` - TaskSpec definition
- `engine/include/writes_effect.h` - writes_effect types
- `engine/include/rowset.h`, `column_batch.h` - RowSet/ColumnBatch
- `engine/include/key_registry.h` - Key registry
- `engine/include/param_registry.h` - Param registry
- `engine/include/feature_registry.h` - Feature registry
- `engine/tests/test_*.cpp` - Unit tests
- `dsl/packages/runtime/src/` - DSL bindings

---

## TODO: Reduce task boilerplate
- Add TaskSpec input-arity contract enforced centrally in executor (remove per-task “expected N inputs” checks).
- Provide helpers for key validation/access (readable/writable + type/nullability) and column fetch (float/string with existence checks).
- For key-based ordering/comparison tasks (sort/dedupe/join), factor a shared comparator/permutation builder handling null ordering and type dispatch.
- Use TaskSpec reads/writes metadata to validate required input keys ahead of task run, moving read checks out of individual tasks.
