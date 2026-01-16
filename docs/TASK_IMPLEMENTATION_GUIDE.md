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
  std::vector<KeyId> writes;               // Static key writes (audit)
  DefaultBudget default_budget;            // Timeout (MVP: ignored)
  OutputPattern output_pattern;            // Output shape contract
  std::optional<WritesEffectExpr> writes_effect; // Param-dependent writes
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

Declares the shape contract for task output:

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

## writes_effect System (RFC 0005)

`writes_effect` declares which keys a task **might write to**, enabling the engine to detect conflicts at link time.

### Why It Exists

- **Static `writes`**: Good for fixed keys, but can't express param-dependent writes
- **`writes_effect`**: Captures "this task writes to whatever `out_key` param says"

### The 4 Options

#### 1. `EffectKeys{}` - Fixed Set (or None)

Use when: Task writes to a **fixed, known set** of keys (or no keys at all).

```cpp
// No writes at all (source tasks, filter, take)
.writes_effect = EffectKeys{}

// Fixed writes to specific keys
.writes_effect = EffectKeys{{4001, 4002}}  // key_ids
```

**When to use:**
- Source tasks (no column writes, schema is static)
- Transform tasks that don't add columns (filter, take, sort)
- Tasks that always write the same keys regardless of params

#### 2. `EffectFromParam{"param_name"}` - Single Key from Param

Use when: Task writes to **one key specified by a param**.

```cpp
// vm writes to whatever out_key param specifies
.writes_effect = EffectFromParam{"out_key"}
```

**When to use:**
- `vm` task: `out_key` param determines output column
- Any task where user specifies a single output key

#### 3. `EffectSwitchEnum{"param", cases}` - Enum-Dependent

Use when: Output keys depend on an **enum param** (like stage).

```cpp
// fetch_features writes different keys per stage
std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
cases["esr"] = makeEffectKeys({4001, 4002});
cases["lsr"] = makeEffectKeys({4003, 4004});

.writes_effect = EffectSwitchEnum{"stage", std::move(cases)}
```

**When to use:**
- Tasks with enum params that change output schema
- Example: `fetch_features` writes ESR columns for stage=esr, LSR columns for stage=lsr

#### 4. `EffectUnion{items}` - Combination

Use when: Multiple write sources need to be **combined**.

```cpp
// Task that writes out_key AND stage-dependent keys
std::vector<std::shared_ptr<WritesEffectExpr>> items;
items.push_back(makeEffectFromParam("out_key"));
items.push_back(makeEffectSwitchEnum("stage", cases));

.writes_effect = EffectUnion{std::move(items)}
```

**When to use:**
- Complex tasks with multiple param-dependent outputs
- Combining fixed keys with param-dependent keys

### Decision Flowchart

```
Does your task write any columns?
├── No → EffectKeys{}
└── Yes → Are the written keys always the same?
    ├── Yes → EffectKeys{{key1, key2, ...}}
    └── No → How are keys determined?
        ├── Single param (like out_key) → EffectFromParam{"param"}
        ├── Enum param (like stage) → EffectSwitchEnum{"param", cases}
        └── Multiple sources → EffectUnion{...}
```

### Evaluation Results

When evaluated with a gamma context (param bindings):

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
      .writes = {},
      .default_budget = {.timeout_ms = 50},
      .output_pattern = OutputPattern::UnaryPreserveView,
      .writes_effect = EffectKeys{},  // Adjust based on decision flowchart
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

### Source Task (No Inputs)

```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "my_source",
    .params_schema = {
      {.name = "fanout", .type = TaskParamType::Int, .required = true},
    },
    .output_pattern = OutputPattern::SourceFanoutDense,
    .writes_effect = EffectKeys{},  // Source tasks have fixed schemas
  };
}

static RowSet run(const std::vector<RowSet>& inputs, ...) {
  if (!inputs.empty()) {
    throw std::runtime_error("my_source: expected 0 inputs");
  }
  // Create and return new ColumnBatch
}
```

### Transform Task (Preserves Rows)

```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "my_transform",
    .params_schema = {
      {.name = "out_key", .type = TaskParamType::Int, .required = true},
      {.name = "expr_id", .type = TaskParamType::ExprId, .required = true},
    },
    .output_pattern = OutputPattern::UnaryPreserveView,
    .writes_effect = EffectFromParam{"out_key"},
  };
}

static RowSet run(const std::vector<RowSet>& inputs, ...) {
  // Add column, return input.withBatch(new_batch)
}
```

### Filter Task (Subsets Rows)

```cpp
static TaskSpec spec() {
  return TaskSpec{
    .op = "my_filter",
    .params_schema = {
      {.name = "pred_id", .type = TaskParamType::PredId, .required = true},
    },
    .output_pattern = OutputPattern::UnarySubsetView,
    .writes_effect = EffectKeys{},  // No column writes
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
| writes_effect types | `engine/include/writes_effect.h` |
| RowSet/ColumnBatch | `engine/include/rowset.h`, `column_batch.h` |
| Key registry | `engine/include/key_registry.h` |
| Param registry | `engine/include/param_registry.h` |
| Unit tests | `engine/tests/test_*.cpp` |
| DSL bindings | `dsl/packages/runtime/src/` |

---

## Checklist

- [ ] Create `engine/src/tasks/<name>.cpp`
- [ ] Implement `spec()` with all required fields
- [ ] Implement `run()` with proper validation
- [ ] Add `TaskRegistrar<T>` at bottom
- [ ] Add to `CMakeLists.txt`
- [ ] Choose correct `writes_effect` (see decision flowchart)
- [ ] Choose correct `output_pattern`
- [ ] Add unit tests
- [ ] Add DSL bindings (if user-facing)
- [ ] Run `./scripts/ci.sh`

---

## See Also

- [PLAN_AUTHORING_GUIDE.md](PLAN_AUTHORING_GUIDE.md) - For ranking engineers
- [ADDING_CAPABILITIES.md](ADDING_CAPABILITIES.md) - For IR extensions
- `engine/include/task_registry.h` - TaskSpec definition
- `engine/include/writes_effect.h` - writes_effect types
