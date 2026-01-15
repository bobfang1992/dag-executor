#include "capability_registry.h"

#include <stdexcept>
#include <unordered_set>

namespace rankd {

// Set of capabilities supported by this engine version.
// Add new capabilities here as they are implemented.
static const std::unordered_set<std::string> supported_capabilities = {
    // RFC0001: Base extensions/capabilities mechanism
    "cap.rfc.0001.extensions_capabilities.v1",
};

bool capability_is_supported(std::string_view cap_id) {
  return supported_capabilities.count(std::string(cap_id)) > 0;
}

void validate_capability_payload(std::string_view cap_id,
                                  const nlohmann::json &payload,
                                  std::string_view scope) {
  // Policy: payloads must be absent (null) or empty object {}
  // This is the strictest policy for the base RFC0001 mechanism.
  // Future capabilities can define their own payload schemas.

  if (payload.is_null()) {
    // Absent payload is OK
    return;
  }

  if (!payload.is_object()) {
    throw std::runtime_error(
        std::string("capability '") + std::string(cap_id) + "' at " +
        std::string(scope) + ": payload must be an object, got " +
        payload.type_name());
  }

  // For now, we accept any object payload without validating contents.
  // Individual capabilities can add stricter validation as needed.
  // The RFC0001 base capability has no required payload fields.
}

} // namespace rankd
