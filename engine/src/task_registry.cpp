#include "task_registry.h"
#include "sha256.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace rankd {

TaskRegistry &TaskRegistry::instance() {
  static TaskRegistry reg;
  return reg;
}

TaskRegistry::TaskRegistry() {
  // Register viewer.follow
  TaskSpec viewer_follow_spec{
      .op = "viewer.follow",
      .params_schema =
          {
              {.name = "fanout", .type = TaskParamType::Int, .required = true},
              {.name = "trace", .type = TaskParamType::String, .required = false},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 100},
  };

  register_task(
      std::move(viewer_follow_spec),
      [](const std::vector<RowSet> &inputs,
         const ValidatedParams &params) -> RowSet {
        if (!inputs.empty()) {
          throw std::runtime_error("viewer.follow: expected 0 inputs");
        }
        int64_t fanout = params.get_int("fanout");
        if (fanout <= 0) {
          throw std::runtime_error("viewer.follow: 'fanout' must be > 0");
        }

        // Create ColumnBatch with ids 1..fanout
        auto batch =
            std::make_shared<ColumnBatch>(static_cast<size_t>(fanout));
        for (int64_t i = 0; i < fanout; ++i) {
          batch->setId(static_cast<size_t>(i), i + 1); // ids are 1-indexed
        }

        return RowSet{
            .batch = batch, .selection = std::nullopt, .order = std::nullopt};
      });

  // Register take
  TaskSpec take_spec{
      .op = "take",
      .params_schema =
          {
              {.name = "count", .type = TaskParamType::Int, .required = true},
              {.name = "trace", .type = TaskParamType::String, .required = false},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 10},
  };

  register_task(
      std::move(take_spec),
      [](const std::vector<RowSet> &inputs,
         const ValidatedParams &params) -> RowSet {
        if (inputs.size() != 1) {
          throw std::runtime_error("take: expected exactly 1 input");
        }
        int64_t count = params.get_int("count");
        if (count <= 0) {
          throw std::runtime_error("take: 'count' must be > 0");
        }

        const auto &input = inputs[0];
        size_t limit = static_cast<size_t>(count);

        // Share the same batch - no column copy!
        RowSet result;
        result.batch = input.batch;

        if (input.order && input.selection) {
          // Both exist: filter order by selection, then truncate
          std::vector<uint8_t> in_selection(input.batch->size(), 0);
          for (uint32_t idx : *input.selection) {
            in_selection[idx] = 1;
          }
          std::vector<uint32_t> filtered;
          for (uint32_t idx : *input.order) {
            if (in_selection[idx]) {
              filtered.push_back(idx);
              if (filtered.size() >= limit)
                break;
            }
          }
          result.selection = std::move(filtered);
          result.order = std::nullopt;
        } else if (input.order) {
          // Only order: truncate order
          auto new_order = *input.order;
          if (new_order.size() > limit) {
            new_order.resize(limit);
          }
          result.order = std::move(new_order);
          result.selection = std::nullopt;
        } else if (input.selection) {
          // Truncate selection
          auto new_selection = *input.selection;
          if (new_selection.size() > limit) {
            new_selection.resize(limit);
          }
          result.selection = std::move(new_selection);
          result.order = std::nullopt;
        } else {
          // Create selection [0..min(count, N)-1]
          size_t n = std::min(limit, input.batch->size());
          std::vector<uint32_t> new_selection;
          new_selection.reserve(n);
          for (size_t i = 0; i < n; ++i) {
            new_selection.push_back(static_cast<uint32_t>(i));
          }
          result.selection = std::move(new_selection);
          result.order = std::nullopt;
        }

        return result;
      });
}

void TaskRegistry::register_task(TaskSpec spec, TaskFn fn) {
  // Governance: no task may declare Key.id in writes
  for (const auto &key : spec.writes) {
    if (key == KeyId::id) {
      throw std::runtime_error("Task '" + spec.op +
                               "' cannot declare Key.id in writes");
    }
  }
  std::string op = spec.op;
  tasks_[op] = TaskEntry{.spec = std::move(spec), .run = std::move(fn)};
}

bool TaskRegistry::has_task(const std::string &op) const {
  return tasks_.find(op) != tasks_.end();
}

const TaskSpec &TaskRegistry::get_spec(const std::string &op) const {
  auto it = tasks_.find(op);
  if (it == tasks_.end()) {
    throw std::runtime_error("Unknown op: " + op);
  }
  return it->second.spec;
}

ValidatedParams TaskRegistry::validate_params(const std::string &op,
                                              const nlohmann::json &params) const {
  const auto &spec = get_spec(op);
  ValidatedParams result;

  // Build set of known param names
  std::unordered_set<std::string> known_params;
  for (const auto &field : spec.params_schema) {
    known_params.insert(field.name);
  }

  // Check for unexpected fields (fail-closed)
  if (params.is_object()) {
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (known_params.find(it.key()) == known_params.end()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': unexpected field '" + it.key() + "'");
      }
    }
  } else if (!params.is_null()) {
    throw std::runtime_error("Invalid params for op '" + op +
                             "': params must be an object or null");
  }

  // Validate each field in schema
  for (const auto &field : spec.params_schema) {
    bool present = params.is_object() && params.contains(field.name);

    if (field.required && !present) {
      throw std::runtime_error("Invalid params for op '" + op +
                               "': missing required field '" + field.name +
                               "'");
    }

    if (present) {
      const auto &value = params[field.name];

      switch (field.type) {
      case TaskParamType::Int: {
        if (!value.is_number_integer()) {
          // Also check if it's a float that happens to be a whole number
          if (value.is_number_float()) {
            double d = value.get<double>();
            if (std::floor(d) != d || d < std::numeric_limits<int64_t>::min() ||
                d > std::numeric_limits<int64_t>::max()) {
              throw std::runtime_error("Invalid params for op '" + op +
                                       "': field '" + field.name +
                                       "' must be int");
            }
            result.int_params[field.name] = static_cast<int64_t>(d);
          } else {
            throw std::runtime_error("Invalid params for op '" + op +
                                     "': field '" + field.name +
                                     "' must be int");
          }
        } else {
          result.int_params[field.name] = value.get<int64_t>();
        }
        break;
      }
      case TaskParamType::Float: {
        if (!value.is_number()) {
          throw std::runtime_error("Invalid params for op '" + op +
                                   "': field '" + field.name +
                                   "' must be float");
        }
        result.float_params[field.name] = value.get<double>();
        break;
      }
      case TaskParamType::Bool: {
        if (!value.is_boolean()) {
          throw std::runtime_error("Invalid params for op '" + op +
                                   "': field '" + field.name +
                                   "' must be bool");
        }
        result.bool_params[field.name] = value.get<bool>();
        break;
      }
      case TaskParamType::String: {
        if (!value.is_string()) {
          throw std::runtime_error("Invalid params for op '" + op +
                                   "': field '" + field.name +
                                   "' must be string");
        }
        result.string_params[field.name] = value.get<std::string>();
        break;
      }
      }
    }
  }

  return result;
}

RowSet TaskRegistry::execute(const std::string &op,
                             const std::vector<RowSet> &inputs,
                             const ValidatedParams &params) const {
  auto it = tasks_.find(op);
  if (it == tasks_.end()) {
    throw std::runtime_error("Unknown op: " + op);
  }
  return it->second.run(inputs, params);
}

std::vector<TaskSpec> TaskRegistry::get_all_specs() const {
  std::vector<TaskSpec> specs;
  specs.reserve(tasks_.size());
  for (const auto &[op, entry] : tasks_) {
    specs.push_back(entry.spec);
  }
  // Sort by op for deterministic ordering
  std::sort(specs.begin(), specs.end(),
            [](const TaskSpec &a, const TaskSpec &b) { return a.op < b.op; });
  return specs;
}

std::string TaskRegistry::compute_manifest_digest() const {
  // Build canonical JSON
  nlohmann::ordered_json manifest;
  manifest["schema_version"] = 1;

  nlohmann::ordered_json tasks_json = nlohmann::json::array();
  auto specs = get_all_specs();

  for (const auto &spec : specs) {
    nlohmann::ordered_json task_json;
    task_json["op"] = spec.op;

    // Sort params by name
    auto sorted_params = spec.params_schema;
    std::sort(sorted_params.begin(), sorted_params.end(),
              [](const ParamField &a, const ParamField &b) {
                return a.name < b.name;
              });

    nlohmann::ordered_json params_json = nlohmann::json::array();
    for (const auto &p : sorted_params) {
      nlohmann::ordered_json pj;
      pj["name"] = p.name;
      switch (p.type) {
      case TaskParamType::Int:
        pj["type"] = "int";
        break;
      case TaskParamType::Float:
        pj["type"] = "float";
        break;
      case TaskParamType::Bool:
        pj["type"] = "bool";
        break;
      case TaskParamType::String:
        pj["type"] = "string";
        break;
      }
      pj["required"] = p.required;
      params_json.push_back(pj);
    }
    task_json["params"] = params_json;

    // reads/writes as arrays of key IDs (empty for now)
    task_json["reads"] = nlohmann::json::array();
    task_json["writes"] = nlohmann::json::array();

    nlohmann::ordered_json budget;
    budget["timeout_ms"] = spec.default_budget.timeout_ms;
    task_json["default_budget"] = budget;

    tasks_json.push_back(task_json);
  }

  manifest["tasks"] = tasks_json;

  // Serialize to compact JSON (no whitespace)
  std::string canonical = manifest.dump();
  return sha256::hash(canonical);
}

} // namespace rankd
