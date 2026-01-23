#pragma once

#include <coroutine>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "async_inflight_limiter.h"
#include "coro_task.h"
#include "endpoint_registry.h"
#include "event_loop.h"

// Forward declarations for hiredis
struct redisAsyncContext;
struct redisReply;

namespace ranking {

/**
 * AsyncRedisClient - async Redis client using hiredis async API + libuv.
 *
 * This client maintains a single persistent connection to a Redis endpoint
 * and provides coroutine awaitables for Redis operations. All operations
 * are non-blocking and integrate with the EventLoop.
 *
 * Thread safety: This class is NOT thread-safe. All operations must be called
 * from the EventLoop thread. The client is designed to be used with coroutines
 * running on the event loop.
 *
 * Fail-fast: No automatic reconnection. If connection fails, operations return
 * errors. The caller can check is_connected() and recreate the client if needed.
 *
 * Usage:
 *   auto client = AsyncRedisClient::Create(loop, spec);
 *   if (!client) { ... handle error ... }
 *
 *   Task<void> doWork() {
 *     auto result = co_await client->HGet("user:123", "name");
 *     if (result) {
 *       std::cout << "Name: " << *result << std::endl;
 *     }
 *   }
 */
class AsyncRedisClient {
 public:
  // Error type for Redis operations
  struct Error {
    std::string message;
    int code = 0;  // hiredis error code (REDIS_ERR_*)
  };

  // Result types
  template <typename T>
  using Result = std::expected<T, Error>;

  /**
   * Create a new async Redis client for the given endpoint.
   *
   * MUST be called on the EventLoop thread.
   * Initiates async connection; use is_connected() or wait for first operation.
   *
   * @param loop EventLoop to use for async operations
   * @param spec Endpoint configuration (host, port, timeouts, inflight limit)
   * @return Client on success, error message on failure
   */
  static std::expected<std::unique_ptr<AsyncRedisClient>, std::string> Create(
      EventLoop& loop, const rankd::EndpointSpec& spec);

  ~AsyncRedisClient();

  // Non-copyable, non-movable
  AsyncRedisClient(const AsyncRedisClient&) = delete;
  AsyncRedisClient& operator=(const AsyncRedisClient&) = delete;
  AsyncRedisClient(AsyncRedisClient&&) = delete;
  AsyncRedisClient& operator=(AsyncRedisClient&&) = delete;

  /**
   * HGET key field - get a hash field value.
   *
   * @return Value if exists, nullopt if field doesn't exist, or error
   */
  Task<Result<std::optional<std::string>>> HGet(std::string_view key, std::string_view field);

  /**
   * LRANGE key start stop - get list elements in range.
   *
   * @return Vector of elements, or error
   */
  Task<Result<std::vector<std::string>>> LRange(std::string_view key, int64_t start, int64_t stop);

  /**
   * HGETALL key - get all hash fields and values.
   *
   * @return Vector of alternating field/value strings, or error
   */
  Task<Result<std::vector<std::string>>> HGetAll(std::string_view key);

  // Connection state accessors
  bool is_connected() const { return connected_; }
  const std::string& endpoint_id() const { return endpoint_id_; }
  const std::string& last_error() const { return last_error_; }

 private:
  // Private constructor - use Create() factory
  AsyncRedisClient(EventLoop& loop, std::string endpoint_id, int max_inflight);

  // Initialize connection (called from Create)
  std::expected<void, std::string> connect(const std::string& host, int port,
                                            int connect_timeout_ms);

  // hiredis async callbacks
  static void OnConnect(const redisAsyncContext* c, int status);
  static void OnDisconnect(const redisAsyncContext* c, int status);

  // Generic command execution
  Task<Result<redisReply*>> execute_command(const char* format, ...);

  EventLoop& loop_;
  redisAsyncContext* ctx_ = nullptr;
  AsyncInflightLimiter limiter_;
  std::string endpoint_id_;
  bool connected_ = false;
  std::string last_error_;
};

/**
 * State for a pending Redis command.
 *
 * Allocated on the heap when a command is issued. Contains the coroutine
 * handle to resume when the reply arrives. Self-destructs after resumption.
 *
 * IMPORTANT: The owning Task must remain alive until the reply arrives.
 * If the Task is destroyed while waiting, the handle becomes dangling.
 */
struct RedisOpState {
  std::coroutine_handle<> handle;
  std::variant<redisReply*, std::string> result;  // Reply pointer or error message
  AsyncInflightLimiter::Guard permit;             // RAII permit release

  // Callback when Redis reply arrives
  static void OnReply(redisAsyncContext* c, void* reply, void* privdata);
};

}  // namespace ranking
