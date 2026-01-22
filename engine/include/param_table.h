#pragma once

#include "param_registry.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace rankd {

// Null tag for explicit null values
struct NullTag {};

// ParamValue: monostate = unset, NullTag = explicit null, or typed value
using ParamValue =
    std::variant<std::monostate, NullTag, int64_t, double, bool, std::string>;

// Lookup ParamMeta by name (linear scan, OK for MVP)
inline const ParamMeta *findParamByName(std::string_view name) {
  for (const auto &meta : kParamRegistry) {
    if (meta.name == name) {
      return &meta;
    }
  }
  return nullptr;
}

// Validation helpers
inline int64_t validateInt(const nlohmann::json &value,
                           const std::string &param_name) {
  if (!value.is_number()) {
    throw std::runtime_error("param '" + param_name + "' must be int");
  }
  if (value.is_number_float()) {
    double d = value.get<double>();
    if (!std::isfinite(d)) {
      throw std::runtime_error("param '" + param_name +
                               "' must be finite number");
    }
    if (std::floor(d) != d) {
      throw std::runtime_error("param '" + param_name + "' must be int");
    }
    if (d < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        d > static_cast<double>(std::numeric_limits<int64_t>::max())) {
      throw std::runtime_error("param '" + param_name +
                               "' out of int64 range");
    }
    return static_cast<int64_t>(d);
  }
  // Check unsigned integers that exceed int64_t max
  if (value.is_number_unsigned()) {
    uint64_t u = value.get<uint64_t>();
    if (u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      throw std::runtime_error("param '" + param_name +
                               "' out of int64 range");
    }
    return static_cast<int64_t>(u);
  }
  return value.get<int64_t>();
}

inline double validateFloat(const nlohmann::json &value,
                            const std::string &param_name) {
  if (!value.is_number()) {
    throw std::runtime_error("param '" + param_name + "' must be float");
  }
  double d = value.get<double>();
  if (!std::isfinite(d)) {
    throw std::runtime_error("param '" + param_name +
                             "' must be finite number");
  }
  return d;
}

inline bool validateBool(const nlohmann::json &value,
                         const std::string &param_name) {
  if (!value.is_boolean()) {
    throw std::runtime_error("param '" + param_name + "' must be bool");
  }
  return value.get<bool>();
}

inline std::string validateString(const nlohmann::json &value,
                                  const std::string &param_name) {
  if (!value.is_string()) {
    throw std::runtime_error("param '" + param_name + "' must be string");
  }
  return value.get<std::string>();
}

class ParamTable {
public:
  // Check if param is set (either value or explicit null)
  bool has(ParamId id) const {
    auto it = values_.find(static_cast<uint32_t>(id));
    return it != values_.end() &&
           !std::holds_alternative<std::monostate>(it->second);
  }

  // Check if param is explicitly null
  bool isNull(ParamId id) const {
    auto it = values_.find(static_cast<uint32_t>(id));
    return it != values_.end() && std::holds_alternative<NullTag>(it->second);
  }

  // Typed getters - return nullopt if unset or null
  std::optional<int64_t> getInt(ParamId id) const {
    auto it = values_.find(static_cast<uint32_t>(id));
    if (it == values_.end()) {
      return std::nullopt;
    }
    if (auto *val = std::get_if<int64_t>(&it->second)) {
      return *val;
    }
    return std::nullopt;
  }

  std::optional<double> getFloat(ParamId id) const {
    auto it = values_.find(static_cast<uint32_t>(id));
    if (it == values_.end()) {
      return std::nullopt;
    }
    if (auto *val = std::get_if<double>(&it->second)) {
      return *val;
    }
    return std::nullopt;
  }

  std::optional<bool> getBool(ParamId id) const {
    auto it = values_.find(static_cast<uint32_t>(id));
    if (it == values_.end()) {
      return std::nullopt;
    }
    if (auto *val = std::get_if<bool>(&it->second)) {
      return *val;
    }
    return std::nullopt;
  }

  std::optional<std::string_view> getString(ParamId id) const {
    auto it = values_.find(static_cast<uint32_t>(id));
    if (it == values_.end()) {
      return std::nullopt;
    }
    if (auto *val = std::get_if<std::string>(&it->second)) {
      return *val;
    }
    return std::nullopt;
  }

  // Set a value
  void set(ParamId id, ParamValue value) {
    values_[static_cast<uint32_t>(id)] = std::move(value);
  }

  // Parse and validate param_overrides from request JSON
  static ParamTable fromParamOverrides(const nlohmann::json &overrides) {
    ParamTable table;

    if (overrides.is_null()) {
      return table;
    }

    if (!overrides.is_object()) {
      throw std::runtime_error("param_overrides must be an object");
    }

    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
      const std::string &name = it.key();
      const auto &value = it.value();

      // Look up param metadata
      const ParamMeta *meta = findParamByName(name);
      if (!meta) {
        throw std::runtime_error("unknown param '" + name + "'");
      }

      // Check allow_write (fail-closed)
      if (!meta->allow_write) {
        throw std::runtime_error("param '" + name + "' is not writable");
      }

      // Check status (fail-closed: only Active params can be overridden)
      if (meta->status != Status::Active) {
        throw std::runtime_error("param '" + name + "' is " +
                                 (meta->status == Status::Deprecated
                                      ? "deprecated"
                                      : "blocked"));
      }

      // Handle null
      if (value.is_null()) {
        if (!meta->nullable) {
          throw std::runtime_error("param '" + name + "' cannot be null");
        }
        table.set(static_cast<ParamId>(meta->id), NullTag{});
        continue;
      }

      // Validate and set based on type
      switch (meta->type) {
      case ParamType::Int:
        table.set(static_cast<ParamId>(meta->id), validateInt(value, name));
        break;
      case ParamType::Float:
        table.set(static_cast<ParamId>(meta->id), validateFloat(value, name));
        break;
      case ParamType::Bool:
        table.set(static_cast<ParamId>(meta->id), validateBool(value, name));
        break;
      case ParamType::String:
        table.set(static_cast<ParamId>(meta->id), validateString(value, name));
        break;
      }
    }

    return table;
  }

private:
  std::unordered_map<uint32_t, ParamValue> values_;
};

// Forward declarations for expr_table and pred_table
struct ExprNode;
using ExprNodePtr = std::shared_ptr<ExprNode>;
struct PredNode;
using PredNodePtr = std::shared_ptr<PredNode>;

// Execution statistics for performance tracking and testing
struct ExecStats {
  uint64_t regex_re2_calls = 0; // Number of RE2 regex evaluations (per dict entry)
};

// Forward declaration for RowSet
class RowSet;

// Forward declaration for RequestContext
struct RequestContext;

// Forward declaration for EndpointRegistry
class EndpointRegistry;

// Forward declaration for IoClients (per-request client cache)
struct IoClients;

// Execution context passed to task run functions
struct ExecCtx {
  const ParamTable *params = nullptr;
  const std::unordered_map<std::string, ExprNodePtr> *expr_table = nullptr;
  const std::unordered_map<std::string, PredNodePtr> *pred_table = nullptr;
  ExecStats *stats = nullptr; // nullable, for instrumentation
  // Resolved NodeRef params: param_name -> RowSet from referenced node
  const std::unordered_map<std::string, RowSet> *resolved_node_refs = nullptr;
  // Request context (user_id, request_id, etc.)
  const RequestContext *request = nullptr;
  // Endpoint registry for IO tasks
  const EndpointRegistry *endpoints = nullptr;
  // Per-request IO client cache (Redis, etc.) - mutable for lazy initialization
  IoClients *clients = nullptr;
  // Enable within-request DAG parallelism (Level 2)
  bool parallel = false;
};

} // namespace rankd
