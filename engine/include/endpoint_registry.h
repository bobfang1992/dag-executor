#pragma once

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "sha256.h"

namespace rankd {

// =====================================================
// Endpoint Types
// =====================================================

enum class EndpointKind { Redis, Http };

enum class ResolverType { Static, Consul, DnsSrv, Https };

struct StaticResolver {
  std::string host;
  int port = 0;
};

struct EndpointPolicy {
  std::optional<int> max_inflight;
  std::optional<int> connect_timeout_ms;
  std::optional<int> request_timeout_ms;
};

struct EndpointSpec {
  std::string endpoint_id;  // "ep_0001" - stable, never reused
  std::string name;         // human-friendly alias
  EndpointKind kind = EndpointKind::Redis;
  ResolverType resolver_type = ResolverType::Static;
  StaticResolver static_resolver;  // only valid when resolver_type==Static
  EndpointPolicy policy;
};

// =====================================================
// EndpointRegistry
// =====================================================

class EndpointRegistry {
 public:
  // Load from JSON file (artifacts/endpoints.<env>.json)
  // Returns either a valid registry or an error message
  // If expected_env is non-empty, the env in the file must match.
  static std::variant<EndpointRegistry, std::string> LoadFromJson(
      const std::string& path, const std::string& expected_env = "");

  // Lookup by endpoint_id (e.g., "ep_0001")
  const EndpointSpec* by_id(std::string_view endpoint_id) const;

  // Lookup by name (e.g., "redis_default")
  const EndpointSpec* by_name(std::string_view name) const;

  // Get all endpoints
  const std::vector<EndpointSpec>& all() const { return entries_; }

  // Get digests
  const std::string& registry_digest() const { return registry_digest_; }
  const std::string& config_digest() const { return config_digest_; }

  // Get environment
  const std::string& env() const { return env_; }

  // Get count
  size_t size() const { return entries_.size(); }

 private:
  std::string env_;
  std::string registry_digest_;
  std::string config_digest_;
  std::vector<EndpointSpec> entries_;
  std::unordered_map<std::string, size_t> by_id_;
  std::unordered_map<std::string, size_t> by_name_;
};

// =====================================================
// Helper functions
// =====================================================

inline std::string_view endpoint_kind_to_string(EndpointKind kind);
inline std::optional<EndpointKind> string_to_endpoint_kind(std::string_view s);
inline std::string_view resolver_type_to_string(ResolverType type);
inline std::optional<ResolverType> string_to_resolver_type(std::string_view s);

// Deterministic JSON stringify (matches dsl/src/codegen/utils.ts stableStringify)
inline std::string stable_stringify(const nlohmann::json& value) {
  if (value.is_null() || value.is_boolean() || value.is_number() ||
      value.is_string()) {
    return value.dump();
  }

  if (value.is_array()) {
    std::string out = "[";
    bool first = true;
    for (const auto& elem : value) {
      if (!first) out += ",";
      first = false;
      out += stable_stringify(elem);
    }
    out += "]";
    return out;
  }

  if (value.is_object()) {
    std::vector<std::string> keys;
    keys.reserve(value.size());
    for (auto it = value.begin(); it != value.end(); ++it) {
      keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());

    std::string out = "{";
    bool first = true;
    for (const auto& key : keys) {
      if (!first) out += ",";
      first = false;
      out += nlohmann::json(key).dump();
      out += ":";
      out += stable_stringify(value.at(key));
    }
    out += "}";
    return out;
  }

  throw std::runtime_error("Unsupported JSON type in stable_stringify");
}

inline std::string compute_digest(const nlohmann::json& value) {
  return sha256::hash(stable_stringify(value));
}

inline std::vector<EndpointSpec> sort_endpoints(std::vector<EndpointSpec> eps) {
  std::sort(eps.begin(), eps.end(),
            [](const EndpointSpec& a, const EndpointSpec& b) {
              return a.endpoint_id < b.endpoint_id;
            });
  return eps;
}

inline nlohmann::json registry_canonical_json(
    const std::vector<EndpointSpec>& endpoints) {
  nlohmann::json entries = nlohmann::json::array();
  for (const auto& ep : sort_endpoints(endpoints)) {
    entries.push_back({{"endpoint_id", ep.endpoint_id},
                       {"name", ep.name},
                       {"kind", endpoint_kind_to_string(ep.kind)}});
  }
  return {{"schema_version", 1}, {"entries", entries}};
}

inline nlohmann::json config_canonical_json(
    const std::vector<EndpointSpec>& endpoints) {
  nlohmann::json entries = nlohmann::json::array();
  for (const auto& ep : sort_endpoints(endpoints)) {
    nlohmann::json policy = nlohmann::json::object();
    if (ep.policy.max_inflight) {
      policy["max_inflight"] = *ep.policy.max_inflight;
    }
    if (ep.policy.connect_timeout_ms) {
      policy["connect_timeout_ms"] = *ep.policy.connect_timeout_ms;
    }
    if (ep.policy.request_timeout_ms) {
      policy["request_timeout_ms"] = *ep.policy.request_timeout_ms;
    }

    nlohmann::json resolver = {
        {"type", resolver_type_to_string(ep.resolver_type)},
        {"host", ep.static_resolver.host},
        {"port", ep.static_resolver.port},
    };

    entries.push_back({{"endpoint_id", ep.endpoint_id},
                       {"name", ep.name},
                       {"kind", endpoint_kind_to_string(ep.kind)},
                       {"resolver", resolver},
                       {"policy", policy}});
  }

  return {{"schema_version", 1}, {"endpoints", entries}};
}

inline std::string_view endpoint_kind_to_string(EndpointKind kind) {
  switch (kind) {
    case EndpointKind::Redis:
      return "redis";
    case EndpointKind::Http:
      return "http";
  }
  return "unknown";
}

inline std::optional<EndpointKind> string_to_endpoint_kind(std::string_view s) {
  if (s == "redis") return EndpointKind::Redis;
  if (s == "http") return EndpointKind::Http;
  return std::nullopt;
}

inline std::string_view resolver_type_to_string(ResolverType type) {
  switch (type) {
    case ResolverType::Static:
      return "static";
    case ResolverType::Consul:
      return "consul";
    case ResolverType::DnsSrv:
      return "dns_srv";
    case ResolverType::Https:
      return "https";
  }
  return "unknown";
}

inline std::optional<ResolverType> string_to_resolver_type(std::string_view s) {
  if (s == "static") return ResolverType::Static;
  if (s == "consul") return ResolverType::Consul;
  if (s == "dns_srv") return ResolverType::DnsSrv;
  if (s == "https") return ResolverType::Https;
  return std::nullopt;
}

// =====================================================
// Implementation (inline for header-only)
// =====================================================

inline std::variant<EndpointRegistry, std::string> EndpointRegistry::LoadFromJson(
    const std::string& path, const std::string& expected_env) {
  // Read file
  std::ifstream file(path);
  if (!file) {
    return "Failed to open endpoint registry: " + path;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(file);
  } catch (const std::exception& e) {
    return "Failed to parse endpoint JSON: " + std::string(e.what());
  }

  // Validate schema version
  if (!j.contains("schema_version") || !j["schema_version"].is_number()) {
    return "Missing or invalid schema_version";
  }
  if (j["schema_version"].get<int>() != 1) {
    return "Unsupported schema_version: " +
           std::to_string(j["schema_version"].get<int>());
  }

  EndpointRegistry registry;

  // Parse env
  if (!j.contains("env") || !j["env"].is_string()) {
    return "Missing or invalid env field";
  }
  registry.env_ = j["env"].get<std::string>();
  if (!expected_env.empty() && registry.env_ != expected_env) {
    return "Env mismatch: expected '" + expected_env + "', got '" +
           registry.env_ + "'";
  }

  // Parse digests
  if (!j.contains("registry_digest") || !j["registry_digest"].is_string()) {
    return "Missing or invalid registry_digest";
  }
  std::string registry_digest_json = j["registry_digest"].get<std::string>();

  if (!j.contains("config_digest") || !j["config_digest"].is_string()) {
    return "Missing or invalid config_digest";
  }
  std::string config_digest_json = j["config_digest"].get<std::string>();

  // Parse endpoints array
  if (!j.contains("endpoints") || !j["endpoints"].is_array()) {
    return "Missing or invalid endpoints array";
  }

  for (const auto& ep_json : j["endpoints"]) {
    EndpointSpec spec;

    // Parse endpoint_id
    if (!ep_json.contains("endpoint_id") ||
        !ep_json["endpoint_id"].is_string()) {
      return "Endpoint missing endpoint_id";
    }
    spec.endpoint_id = ep_json["endpoint_id"].get<std::string>();

    // Validate endpoint_id format
    if (!spec.endpoint_id.starts_with("ep_")) {
      return "endpoint_id must start with 'ep_': " + spec.endpoint_id;
    }
    if (spec.endpoint_id.size() > 64) {
      return "endpoint_id too long (max 64): " + spec.endpoint_id;
    }

    // Parse name
    if (!ep_json.contains("name") || !ep_json["name"].is_string()) {
      return "Endpoint " + spec.endpoint_id + " missing name";
    }
    spec.name = ep_json["name"].get<std::string>();

    // Parse kind
    if (!ep_json.contains("kind") || !ep_json["kind"].is_string()) {
      return "Endpoint " + spec.endpoint_id + " missing kind";
    }
    auto kind_str = ep_json["kind"].get<std::string>();
    auto kind_opt = string_to_endpoint_kind(kind_str);
    if (!kind_opt) {
      return "Endpoint " + spec.endpoint_id + " has unknown kind: " + kind_str;
    }
    spec.kind = *kind_opt;

    // Parse resolver
    if (!ep_json.contains("resolver") || !ep_json["resolver"].is_object()) {
      return "Endpoint " + spec.endpoint_id + " missing resolver";
    }
    const auto& resolver = ep_json["resolver"];

    if (!resolver.contains("type") || !resolver["type"].is_string()) {
      return "Endpoint " + spec.endpoint_id + " resolver missing type";
    }
    auto resolver_type_str = resolver["type"].get<std::string>();
    auto resolver_type_opt = string_to_resolver_type(resolver_type_str);
    if (!resolver_type_opt) {
      return "Endpoint " + spec.endpoint_id +
             " has unknown resolver type: " + resolver_type_str;
    }
    spec.resolver_type = *resolver_type_opt;

    // Step 14.2: Only static resolver is supported
    if (spec.resolver_type != ResolverType::Static) {
      return "Endpoint " + spec.endpoint_id +
             ": only 'static' resolver supported in Step 14.2, got: " +
             resolver_type_str;
    }

    // Parse static resolver fields
    if (!resolver.contains("host") || !resolver["host"].is_string()) {
      return "Endpoint " + spec.endpoint_id + " resolver missing host";
    }
    spec.static_resolver.host = resolver["host"].get<std::string>();

    if (!resolver.contains("port") || !resolver["port"].is_number_integer()) {
      return "Endpoint " + spec.endpoint_id + " resolver missing port";
    }
    spec.static_resolver.port = resolver["port"].get<int>();

    // Validate port range
    if (spec.static_resolver.port < 1 || spec.static_resolver.port > 65535) {
      return "Endpoint " + spec.endpoint_id + " has invalid port: " +
             std::to_string(spec.static_resolver.port);
    }

    // Parse policy (optional)
    if (ep_json.contains("policy") && ep_json["policy"].is_object()) {
      const auto& policy = ep_json["policy"];
      if (policy.contains("max_inflight") &&
          policy["max_inflight"].is_number_integer()) {
        spec.policy.max_inflight = policy["max_inflight"].get<int>();
      }
      if (policy.contains("connect_timeout_ms") &&
          policy["connect_timeout_ms"].is_number_integer()) {
        spec.policy.connect_timeout_ms =
            policy["connect_timeout_ms"].get<int>();
      }
      if (policy.contains("request_timeout_ms") &&
          policy["request_timeout_ms"].is_number_integer()) {
        spec.policy.request_timeout_ms =
            policy["request_timeout_ms"].get<int>();
      }
    }

    // Check for duplicate endpoint_id
    if (registry.by_id_.contains(spec.endpoint_id)) {
      return "Duplicate endpoint_id: " + spec.endpoint_id;
    }

    // Check for duplicate name
    if (registry.by_name_.contains(spec.name)) {
      return "Duplicate endpoint name: " + spec.name;
    }

    // Add to registry
    size_t idx = registry.entries_.size();
    registry.by_id_[spec.endpoint_id] = idx;
    registry.by_name_[spec.name] = idx;
    registry.entries_.push_back(std::move(spec));
  }

  // Compute digests from parsed entries (trust the data, not the file fields)
  registry.registry_digest_ =
      compute_digest(registry_canonical_json(registry.entries_));
  registry.config_digest_ =
      compute_digest(config_canonical_json(registry.entries_));

  // Validate provided digests
  if (registry.registry_digest_ != registry_digest_json) {
    return "registry_digest mismatch for env '" + registry.env_ +
           "': expected " + registry_digest_json + ", computed " +
           registry.registry_digest_;
  }
  if (registry.config_digest_ != config_digest_json) {
    return "config_digest mismatch for env '" + registry.env_ +
           "': expected " + config_digest_json + ", computed " +
           registry.config_digest_;
  }

  return registry;
}

inline const EndpointSpec* EndpointRegistry::by_id(
    std::string_view endpoint_id) const {
  auto it = by_id_.find(std::string(endpoint_id));
  if (it == by_id_.end()) return nullptr;
  return &entries_[it->second];
}

inline const EndpointSpec* EndpointRegistry::by_name(
    std::string_view name) const {
  auto it = by_name_.find(std::string(name));
  if (it == by_name_.end()) return nullptr;
  return &entries_[it->second];
}

}  // namespace rankd
