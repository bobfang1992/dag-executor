#include "task_registry.h"
#include "endpoint_registry.h"
#include "sha256.h"
#include "writes_effect.h"
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

WritesEffectExpr compute_effective_writes(const TaskSpec &spec) {
  bool has_static = !spec.writes.empty();
  bool has_dynamic = spec.writes_effect.has_value();

  // Convert KeyId vector to uint32_t vector
  auto to_uint32_vec = [](const std::vector<KeyId> &keys) {
    std::vector<uint32_t> result;
    result.reserve(keys.size());
    for (const auto &k : keys) {
      result.push_back(static_cast<uint32_t>(k));
    }
    return result;
  };

  if (!has_static && !has_dynamic) {
    // No writes at all
    return EffectKeys{};
  }

  if (has_static && !has_dynamic) {
    // Only static writes
    return EffectKeys{to_uint32_vec(spec.writes)};
  }

  if (!has_static && has_dynamic) {
    // Only dynamic writes
    return *spec.writes_effect;
  }

  // Both static and dynamic: union them
  std::vector<std::shared_ptr<WritesEffectExpr>> items;
  items.push_back(
      std::make_shared<WritesEffectExpr>(EffectKeys{to_uint32_vec(spec.writes)}));
  items.push_back(std::make_shared<WritesEffectExpr>(*spec.writes_effect));
  return EffectUnion{std::move(items)};
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

ValidatedParams
TaskRegistry::validate_params(const std::string &op,
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

  // Helper to apply default value
  auto apply_default = [&result](const ParamField &field) {
    if (!field.default_value) {
      return;
    }
    const auto &def = *field.default_value;
    switch (field.type) {
    case TaskParamType::Int:
      result.int_params[field.name] = std::get<int64_t>(def);
      break;
    case TaskParamType::Float:
      result.float_params[field.name] = std::get<double>(def);
      break;
    case TaskParamType::Bool:
      result.bool_params[field.name] = std::get<bool>(def);
      break;
    case TaskParamType::String:
    case TaskParamType::ExprId:
    case TaskParamType::PredId:
      result.string_params[field.name] = std::get<std::string>(def);
      break;
    case TaskParamType::NodeRef:
      result.node_ref_params[field.name] = std::get<std::string>(def);
      break;
    case TaskParamType::EndpointRef:
      result.string_params[field.name] = std::get<std::string>(def);
      break;
    }
  };

  // Validate each field in schema
  for (const auto &field : spec.params_schema) {
    bool present = params.is_object() && params.contains(field.name);
    bool is_null = present && params[field.name].is_null();

    // Handle absent or null values
    if (!present || is_null) {
      if (field.required) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': missing required field '" + field.name +
                                 "'");
      }
      if (field.default_value) {
        // Use default value
        apply_default(field);
      } else if (!field.nullable) {
        // Not nullable and no default - fail for null, skip for absent
        if (is_null) {
          throw std::runtime_error("Invalid params for op '" + op +
                                   "': field '" + field.name +
                                   "' cannot be null");
        }
        // Absent optional without default - just skip
      }
      // else: nullable without default - skip (no value in ValidatedParams)
      continue;
    }

    // Value is present and not null - validate type
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
                                   "': field '" + field.name + "' must be int");
        }
      } else {
        result.int_params[field.name] = value.get<int64_t>();
      }
      break;
    }
    case TaskParamType::Float: {
      if (!value.is_number()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name + "' must be float");
      }
      result.float_params[field.name] = value.get<double>();
      break;
    }
    case TaskParamType::Bool: {
      if (!value.is_boolean()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name + "' must be bool");
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
    case TaskParamType::ExprId:
    case TaskParamType::PredId: {
      // ExprId/PredId are stored as strings; validation against
      // expr_table/pred_table happens in validate_plan
      if (!value.is_string()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name +
                                 "' must be string");
      }
      result.string_params[field.name] = value.get<std::string>();
      break;
    }
    case TaskParamType::NodeRef: {
      // NodeRef is stored as string (node_id); validation against
      // existing nodes happens in validate_plan
      if (!value.is_string()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name +
                                 "' must be string (node_id)");
      }
      result.node_ref_params[field.name] = value.get<std::string>();
      break;
    }
    case TaskParamType::EndpointRef: {
      // EndpointRef is stored as string (endpoint_id); validation against
      // EndpointRegistry happens in validate_plan
      if (!value.is_string()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name +
                                 "' must be string (endpoint_id)");
      }
      result.string_params[field.name] = value.get<std::string>();
      break;
    }
    }
  }

  return result;
}

RowSet TaskRegistry::execute(const std::string &op,
                             const std::vector<RowSet> &inputs,
                             const ValidatedParams &params,
                             const ExecCtx &ctx) const {
  auto it = tasks_.find(op);
  if (it == tasks_.end()) {
    throw std::runtime_error("Unknown op: " + op);
  }
  return it->second.run(inputs, params, ctx);
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
      // Use alphabetical order for deterministic JSON
      if (p.default_value) {
        std::visit([&pj](const auto &v) { pj["default"] = v; },
                   *p.default_value);
      }
      pj["name"] = p.name;
      pj["nullable"] = p.nullable;
      pj["required"] = p.required;
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
      case TaskParamType::ExprId:
        pj["type"] = "expr_id";
        break;
      case TaskParamType::PredId:
        pj["type"] = "pred_id";
        break;
      case TaskParamType::NodeRef:
        pj["type"] = "node_ref";
        break;
      case TaskParamType::EndpointRef:
        pj["type"] = "endpoint_ref";
        if (p.endpoint_kind) {
          pj["endpoint_kind"] = endpoint_kind_to_string(*p.endpoint_kind);
        }
        break;
      }
      params_json.push_back(pj);
    }
    task_json["params"] = params_json;

    // reads/writes as sorted arrays of key IDs
    auto sorted_reads = spec.reads;
    std::sort(sorted_reads.begin(), sorted_reads.end());
    nlohmann::ordered_json reads_json = nlohmann::json::array();
    for (const auto &key : sorted_reads) {
      reads_json.push_back(static_cast<uint32_t>(key));
    }
    task_json["reads"] = reads_json;

    auto sorted_writes = spec.writes;
    std::sort(sorted_writes.begin(), sorted_writes.end());
    nlohmann::ordered_json writes_json = nlohmann::json::array();
    for (const auto &key : sorted_writes) {
      writes_json.push_back(static_cast<uint32_t>(key));
    }
    task_json["writes"] = writes_json;

    nlohmann::ordered_json budget;
    budget["timeout_ms"] = spec.default_budget.timeout_ms;
    task_json["default_budget"] = budget;

    // Add output_pattern for deterministic manifest
    task_json["output_pattern"] = outputPatternToString(spec.output_pattern);

    // Add writes_effect if present (RFC0005)
    if (spec.writes_effect) {
      task_json["writes_effect"] =
          nlohmann::json::parse(serialize_writes_effect(*spec.writes_effect));
    }

    tasks_json.push_back(task_json);
  }

  manifest["tasks"] = tasks_json;

  // Serialize to compact JSON (no whitespace)
  std::string canonical = manifest.dump();
  return sha256::hash(canonical);
}

std::string TaskRegistry::to_toml() const {
  std::ostringstream out;
  out << "# AUTO-GENERATED from C++ TaskSpec - DO NOT EDIT\n";
  out << "# Regenerate with: engine/bin/rankd --print-task-manifest > "
         "registry/tasks.toml\n";
  out << "schema_version = 1\n";
  out << "manifest_digest = \"" << compute_manifest_digest() << "\"\n";
  out << "\n";

  auto specs = get_all_specs(); // sorted by op

  for (const auto &spec : specs) {
    out << "[[task]]\n";
    out << "op = \"" << spec.op << "\"\n";
    out << "output_pattern = \"" << outputPatternToString(spec.output_pattern)
        << "\"\n";

    // writes_effect if present
    if (spec.writes_effect) {
      out << "writes_effect = \"\"\"\n"
          << serialize_writes_effect(*spec.writes_effect) << "\n\"\"\"\n";
    }

    // params
    // Sort params by name for deterministic output
    auto sorted_params = spec.params_schema;
    std::sort(sorted_params.begin(), sorted_params.end(),
              [](const ParamField &a, const ParamField &b) {
                return a.name < b.name;
              });

    for (const auto &p : sorted_params) {
      out << "\n  [[task.param]]\n";
      out << "  name = \"" << p.name << "\"\n";

      // type string
      switch (p.type) {
      case TaskParamType::Int:
        out << "  type = \"int\"\n";
        break;
      case TaskParamType::Float:
        out << "  type = \"float\"\n";
        break;
      case TaskParamType::Bool:
        out << "  type = \"bool\"\n";
        break;
      case TaskParamType::String:
        out << "  type = \"string\"\n";
        break;
      case TaskParamType::ExprId:
        out << "  type = \"expr_id\"\n";
        break;
      case TaskParamType::PredId:
        out << "  type = \"pred_id\"\n";
        break;
      case TaskParamType::NodeRef:
        out << "  type = \"node_ref\"\n";
        break;
      case TaskParamType::EndpointRef:
        out << "  type = \"endpoint_ref\"\n";
        if (p.endpoint_kind) {
          out << "  endpoint_kind = \"" << endpoint_kind_to_string(*p.endpoint_kind) << "\"\n";
        }
        break;
      }

      out << "  required = " << (p.required ? "true" : "false") << "\n";
      out << "  nullable = " << (p.nullable ? "true" : "false") << "\n";
    }

    out << "\n";
  }

  return out.str();
}

} // namespace rankd
