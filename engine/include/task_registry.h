#pragma once

#include "key_registry.h"
#include "output_contract.h"
#include "rowset.h"
#include "writes_effect.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rankd {

// Forward declaration
struct ExecCtx;

// Param types supported in task param schemas (named TaskParamType to avoid
// conflict with generated ParamType in param_registry.h)
enum class TaskParamType {
  Int,
  Float,
  Bool,
  String,
  ExprId,  // Reference to expr_table entry (validated at plan load)
  PredId,  // Reference to pred_table entry (validated at plan load)
  NodeRef  // Reference to another node in the DAG (resolved by executor)
};

// Default value type for task params
using ParamDefaultValue = std::variant<int64_t, double, bool, std::string>;

// A single parameter field in a task's param schema
struct ParamField {
  std::string name;
  TaskParamType type;
  bool required;
  bool nullable = false; // if true, null is a valid value
  std::optional<ParamDefaultValue> default_value; // used when absent or null
};

// Default budget for task execution (MVP: included but ignored by executor)
struct DefaultBudget {
  int timeout_ms;
};

// Task specification - the single source of truth for task validation
struct TaskSpec {
  std::string op;
  std::vector<ParamField> params_schema;
  std::vector<KeyId> reads;
  std::vector<KeyId> writes;
  DefaultBudget default_budget;
  OutputPattern output_pattern; // Required output shape contract
  std::optional<WritesEffectExpr> writes_effect; // RFC0005: param-dependent writes
};

// Compute the effective writes contract expression from a TaskSpec.
// This combines static writes and dynamic writes_effect into a single expression:
//   - If both empty: returns EffectKeys{}
//   - If only writes: returns EffectKeys{writes}
//   - If only writes_effect: returns *writes_effect
//   - If both: returns EffectUnion{Keys(writes), *writes_effect}
WritesEffectExpr compute_effective_writes(const TaskSpec &spec);

// Validated parameters - stored after validation so run functions don't
// re-parse
struct ValidatedParams {
  std::unordered_map<std::string, int64_t> int_params;
  std::unordered_map<std::string, double> float_params;
  std::unordered_map<std::string, bool> bool_params;
  std::unordered_map<std::string, std::string> string_params;
  std::unordered_map<std::string, std::string> node_ref_params;  // NodeRef: param name -> node_id

  bool has_int(const std::string &name) const {
    return int_params.find(name) != int_params.end();
  }
  bool has_float(const std::string &name) const {
    return float_params.find(name) != float_params.end();
  }
  bool has_bool(const std::string &name) const {
    return bool_params.find(name) != bool_params.end();
  }
  bool has_string(const std::string &name) const {
    return string_params.find(name) != string_params.end();
  }
  bool has_node_ref(const std::string &name) const {
    return node_ref_params.find(name) != node_ref_params.end();
  }

  int64_t get_int(const std::string &name) const {
    return int_params.at(name);
  }
  double get_float(const std::string &name) const {
    return float_params.at(name);
  }
  bool get_bool(const std::string &name) const { return bool_params.at(name); }
  const std::string &get_string(const std::string &name) const {
    return string_params.at(name);
  }
  const std::string &get_node_ref(const std::string &name) const {
    return node_ref_params.at(name);
  }
};

// Task function signature: (inputs, validated_params, exec_ctx) -> output
using TaskFn = std::function<RowSet(const std::vector<RowSet> &,
                                    const ValidatedParams &, const ExecCtx &)>;

// Combined task entry: spec + run function
struct TaskEntry {
  TaskSpec spec;
  TaskFn run;
};

class TaskRegistry {
public:
  static TaskRegistry &instance();

  void register_task(TaskSpec spec, TaskFn fn);
  bool has_task(const std::string &op) const;
  const TaskSpec &get_spec(const std::string &op) const;

  // Validate params against spec, returns validated params or throws
  ValidatedParams validate_params(const std::string &op,
                                  const nlohmann::json &params) const;

  // Execute task with pre-validated params
  RowSet execute(const std::string &op, const std::vector<RowSet> &inputs,
                 const ValidatedParams &params, const ExecCtx &ctx) const;

  // Get all task specs (for manifest digest)
  std::vector<TaskSpec> get_all_specs() const;

  // Compute task manifest digest
  std::string compute_manifest_digest() const;

  // Generate TOML representation for codegen
  std::string to_toml() const;

  // Get number of registered tasks
  size_t num_tasks() const { return tasks_.size(); }

private:
  TaskRegistry() = default;
  std::unordered_map<std::string, TaskEntry> tasks_;
};

// =============================================================================
// TaskRegistrar: Auto-registration helper for class-based tasks
// =============================================================================
//
// Each task class should define:
//   static TaskSpec spec();
//   static RowSet run(const std::vector<RowSet>&, const ValidatedParams&, const ExecCtx&);
//
// Then add at the bottom of the task .cpp file:
//   static TaskRegistrar<MyTask> registrar;
//
// This will auto-register the task when the translation unit is loaded.
//
template <typename T> class TaskRegistrar {
public:
  TaskRegistrar() { TaskRegistry::instance().register_task(T::spec(), T::run); }
};

} // namespace rankd
