#pragma once

#include "key_registry.h"
#include "rowset.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rankd {

// Param types supported in task param schemas (named TaskParamType to avoid
// conflict with generated ParamType in param_registry.h)
enum class TaskParamType { Int, Float, Bool, String };

// A single parameter field in a task's param schema
struct ParamField {
  std::string name;
  TaskParamType type;
  bool required;
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
};

// Validated parameters - stored after validation so run functions don't
// re-parse
struct ValidatedParams {
  std::unordered_map<std::string, int64_t> int_params;
  std::unordered_map<std::string, double> float_params;
  std::unordered_map<std::string, bool> bool_params;
  std::unordered_map<std::string, std::string> string_params;

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
};

// Task function signature: (inputs, validated_params) -> output
using TaskFn =
    std::function<RowSet(const std::vector<RowSet> &, const ValidatedParams &)>;

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
                 const ValidatedParams &params) const;

  // Get all task specs (for manifest digest)
  std::vector<TaskSpec> get_all_specs() const;

  // Compute task manifest digest
  std::string compute_manifest_digest() const;

  // Get number of registered tasks
  size_t num_tasks() const { return tasks_.size(); }

private:
  TaskRegistry();
  std::unordered_map<std::string, TaskEntry> tasks_;
};

} // namespace rankd
