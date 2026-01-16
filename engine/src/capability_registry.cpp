#include "capability_registry.h"

#include "capability_registry_gen.h"
#include "sha256.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>

namespace rankd {

// Build supported capabilities set from generated registry (once at startup)
static std::unordered_set<std::string> build_supported_caps() {
  std::unordered_set<std::string> caps;
  for (const auto &meta : kCapabilityRegistry) {
    // Include all capabilities except blocked ones
    if (meta.status != CapabilityStatus::Blocked) {
      caps.insert(std::string(meta.id));
    }
  }
  return caps;
}

static const std::unordered_set<std::string> supported_capabilities =
    build_supported_caps();

bool capability_is_supported(std::string_view cap_id) {
  return supported_capabilities.count(std::string(cap_id)) > 0;
}

// Helper: lookup capability metadata from generated registry
static const CapabilityMeta *find_capability_meta(std::string_view cap_id) {
  for (const auto &meta : kCapabilityRegistry) {
    if (meta.id == cap_id)
      return &meta;
  }
  return nullptr;
}

void validate_capability_payload(std::string_view cap_id,
                                  const nlohmann::json &payload,
                                  std::string_view scope) {
  const auto *meta = find_capability_meta(cap_id);
  if (!meta)
    return; // Unknown cap handled by capability_is_supported

  const auto &schema = meta->schema;

  // No schema = no payload allowed
  if (!schema.has_schema) {
    if (!payload.is_null()) {
      throw std::runtime_error(std::string("capability '") +
                                std::string(cap_id) + "' at " +
                                std::string(scope) + ": no payload allowed");
    }
    return;
  }

  // Absent payload is always OK when schema exists
  if (payload.is_null())
    return;

  if (!payload.is_object()) {
    throw std::runtime_error(
        std::string("capability '") + std::string(cap_id) + "' at " +
        std::string(scope) + ": payload must be an object, got " +
        payload.type_name());
  }

  // Check additional properties (schema-driven validation)
  if (!schema.additional_properties && !payload.empty()) {
    // additionalProperties: false means no extra keys allowed
    // For RFC0001 base capability: no properties defined, so must be empty {}
    throw std::runtime_error(
        std::string("capability '") + std::string(cap_id) + "' at " +
        std::string(scope) +
        ": payload must be empty object {}, got object with " +
        std::to_string(payload.size()) + " field(s)");
  }
}

// Helper: produce canonical JSON string with sorted keys (no whitespace)
// NOTE: Float formatting may differ between C++ (j.dump()) and JS (JSON.stringify)
// for edge cases like 1.0 vs "1" or scientific notation. Current capability payloads
// don't use floats. If floats are needed in the future, implement JCS or align formatting.
static std::string canonical_json_string(const nlohmann::json &j) {
  if (j.is_null()) {
    return "null";
  }
  if (j.is_boolean()) {
    return j.get<bool>() ? "true" : "false";
  }
  if (j.is_number_integer()) {
    return std::to_string(j.get<int64_t>());
  }
  if (j.is_number_unsigned()) {
    return std::to_string(j.get<uint64_t>());
  }
  if (j.is_number_float()) {
    // Use nlohmann's dump for proper float formatting
    return j.dump();
  }
  if (j.is_string()) {
    // Use nlohmann's dump for proper escaping
    return j.dump();
  }
  if (j.is_array()) {
    std::string result = "[";
    bool first = true;
    for (const auto &elem : j) {
      if (!first)
        result += ",";
      first = false;
      result += canonical_json_string(elem);
    }
    result += "]";
    return result;
  }
  if (j.is_object()) {
    // Collect keys and sort them
    std::vector<std::string> keys;
    for (auto it = j.begin(); it != j.end(); ++it) {
      keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());

    std::string result = "{";
    bool first = true;
    for (const auto &key : keys) {
      if (!first)
        result += ",";
      first = false;
      // Use nlohmann's dump for proper key escaping
      result += nlohmann::json(key).dump();
      result += ":";
      result += canonical_json_string(j.at(key));
    }
    result += "}";
    return result;
  }
  // Fallback (should not reach)
  return j.dump();
}

std::string compute_capabilities_digest(
    const std::vector<std::string> &capabilities_required,
    const nlohmann::json &extensions) {
  // If both are empty, return empty string (no capabilities)
  bool caps_empty = capabilities_required.empty();
  bool exts_empty = extensions.is_null() || (extensions.is_object() && extensions.empty());

  if (caps_empty && exts_empty) {
    return "";
  }

  // Build canonical object: {"capabilities_required":[...],"extensions":{...}}
  // Keys are already in alphabetical order: "capabilities_required" < "extensions"
  nlohmann::json canonical;
  canonical["capabilities_required"] = capabilities_required;
  canonical["extensions"] = exts_empty ? nlohmann::json::object() : extensions;

  // Produce canonical JSON string
  std::string canonical_str = canonical_json_string(canonical);

  // Compute SHA256
  std::string hash = sha256::hash(canonical_str);

  return "sha256:" + hash;
}

} // namespace rankd
