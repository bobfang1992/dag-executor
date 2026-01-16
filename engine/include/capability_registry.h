#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace rankd {

/**
 * Check if a capability is supported by this engine version.
 *
 * Returns true if the capability is known and supported.
 * Returns false if the capability is unknown (plan should be rejected).
 */
bool capability_is_supported(std::string_view cap_id);

/**
 * Validate the payload for a capability.
 *
 * @param cap_id The capability identifier
 * @param payload The extension payload (may be null/empty)
 * @param scope Diagnostic context: "plan" or "node:<node_id>"
 *
 * Throws std::runtime_error if payload is invalid for the capability.
 *
 * Policy: Payloads must be empty objects {} or absent (null).
 * This is the strictest policy - capabilities that need payload data
 * should define their own schema validation in future versions.
 */
void validate_capability_payload(std::string_view cap_id,
                                  const nlohmann::json &payload,
                                  std::string_view scope);

/**
 * Compute capabilities digest for RFC0001.
 *
 * Returns "sha256:<hex>" of canonical JSON, or "" if both fields are
 * empty/absent.
 *
 * Canonical form: {"capabilities_required":[...],"extensions":{...}}
 * - Keys sorted alphabetically ("capabilities_required" < "extensions")
 * - Empty arrays/objects normalized to [] and {}
 *
 * This must produce identical output to the TypeScript implementation
 * for cross-language parity.
 */
std::string compute_capabilities_digest(
    const std::vector<std::string> &capabilities_required,
    const nlohmann::json &extensions);

} // namespace rankd
