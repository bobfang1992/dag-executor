#pragma once

#include "endpoint_registry.h"
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration for hiredis
struct redisContext;

namespace rankd {

// Redis client wrapper using hiredis
// Uses std::expected for error handling (C++23)
class RedisClient {
 public:
  // Create a client for the given endpoint spec
  explicit RedisClient(const EndpointSpec& endpoint);
  ~RedisClient();

  // Non-copyable, movable
  RedisClient(const RedisClient&) = delete;
  RedisClient& operator=(const RedisClient&) = delete;
  RedisClient(RedisClient&& other) noexcept;
  RedisClient& operator=(RedisClient&& other) noexcept;

  // LRANGE key start stop - get list elements
  // Returns vector of strings on success, error message on failure
  std::expected<std::vector<std::string>, std::string> lrange(
      const std::string& key, int64_t start, int64_t stop);

  // HGETALL key - get all hash fields and values
  // Returns map of field->value on success, error message on failure
  std::expected<std::unordered_map<std::string, std::string>, std::string>
  hgetall(const std::string& key);

  // Check if connected
  bool connected() const { return ctx_ != nullptr; }

  // Get last error message (if any)
  const std::string& last_error() const { return last_error_; }

 private:
  // Lazy connect on first command
  std::expected<void, std::string> ensure_connected();

  // Disconnect and clear state
  void disconnect();

  std::string host_;
  int port_ = 0;
  int connect_timeout_ms_ = 50;
  int request_timeout_ms_ = 20;
  redisContext* ctx_ = nullptr;
  std::string last_error_;
};

}  // namespace rankd
