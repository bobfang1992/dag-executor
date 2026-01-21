#pragma once

#include <charconv>
#include <cstdint>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rankd {

/**
 * RequestContext - execution-layer context for a rank request.
 * Accessible from ExecCtx during task execution.
 */
struct RequestContext {
  std::string request_id;
  uint32_t user_id = 0;
};

/**
 * ParseResult - result of parsing a rank request.
 * Contains either a valid RequestContext or an error message.
 */
struct ParseResult {
  bool ok = false;
  RequestContext context;
  std::string error;

  static ParseResult success(RequestContext ctx) {
    ParseResult r;
    r.ok = true;
    r.context = std::move(ctx);
    return r;
  }

  static ParseResult failure(std::string msg) {
    ParseResult r;
    r.ok = false;
    r.error = std::move(msg);
    return r;
  }
};

/**
 * Parse user_id from JSON value.
 * Accepts:
 * - Positive integer (1 to UINT32_MAX)
 * - String containing decimal integer in valid range
 * Rejects:
 * - Missing, null, bool, object, array, float
 * - Zero or negative values
 * - Strings with non-decimal content
 */
inline std::optional<uint32_t> parse_user_id(const nlohmann::json& value, std::string& error) {
  if (value.is_null()) {
    error = "invalid type for user_id: expected positive integer or numeric string, got null";
    return std::nullopt;
  }

  if (value.is_boolean()) {
    error = "invalid type for user_id: expected positive integer or numeric string, got boolean";
    return std::nullopt;
  }

  if (value.is_object()) {
    error = "invalid type for user_id: expected positive integer or numeric string, got object";
    return std::nullopt;
  }

  if (value.is_array()) {
    error = "invalid type for user_id: expected positive integer or numeric string, got array";
    return std::nullopt;
  }

  if (value.is_number_float()) {
    error = "invalid type for user_id: expected positive integer or numeric string, got float";
    return std::nullopt;
  }

  if (value.is_number_integer()) {
    auto num = value.get<int64_t>();
    if (num <= 0) {
      error = "invalid user_id: must be positive integer (got " + std::to_string(num) + ")";
      return std::nullopt;
    }
    if (num > static_cast<int64_t>(UINT32_MAX)) {
      error = "invalid user_id: value " + std::to_string(num) + " exceeds uint32 max";
      return std::nullopt;
    }
    return static_cast<uint32_t>(num);
  }

  if (value.is_string()) {
    const std::string& str = value.get_ref<const std::string&>();
    if (str.empty()) {
      error = "invalid user_id: empty string";
      return std::nullopt;
    }

    // Use std::from_chars for strict decimal parsing
    uint64_t parsed = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), parsed);

    if (ec != std::errc{} || ptr != str.data() + str.size()) {
      error = "invalid user_id: string \"" + str + "\" is not a valid decimal integer";
      return std::nullopt;
    }

    if (parsed == 0) {
      error = "invalid user_id: must be positive integer (got 0)";
      return std::nullopt;
    }

    if (parsed > UINT32_MAX) {
      error = "invalid user_id: value " + std::to_string(parsed) + " exceeds uint32 max";
      return std::nullopt;
    }

    return static_cast<uint32_t>(parsed);
  }

  error = "invalid type for user_id: unexpected JSON type";
  return std::nullopt;
}

/**
 * Generate a simple UUID for request_id.
 * (Reuses existing generate_uuid logic from main.cpp)
 */
inline std::string generate_request_id() {
  // Simple UUID generation - same as existing generate_uuid in main.cpp
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dis;

  auto rand_hex = [&](int len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < len; i += 8) {
      ss << std::setw(8) << dis(gen);
    }
    return ss.str().substr(0, len);
  };

  return rand_hex(8) + "-" + rand_hex(4) + "-" + rand_hex(4) + "-" +
         rand_hex(4) + "-" + rand_hex(12);
}

/**
 * Parse a rank request JSON into RequestContext.
 *
 * Required fields:
 * - user_id: positive uint32 (as integer or string)
 *
 * Optional fields:
 * - request_id: string (generated if missing)
 *
 * @param request The JSON request object
 * @return ParseResult with either success(context) or failure(error)
 */
inline ParseResult parse_request_context(const nlohmann::json& request) {
  RequestContext ctx;

  // Parse request_id (optional, generate if missing)
  if (request.contains("request_id") && request["request_id"].is_string()) {
    ctx.request_id = request["request_id"].get<std::string>();
  } else {
    ctx.request_id = generate_request_id();
  }

  // Parse user_id (required)
  if (!request.contains("user_id")) {
    return ParseResult::failure("missing required field: user_id");
  }

  std::string error;
  auto user_id = parse_user_id(request["user_id"], error);
  if (!user_id.has_value()) {
    return ParseResult::failure(error);
  }
  ctx.user_id = user_id.value();

  return ParseResult::success(std::move(ctx));
}

} // namespace rankd
