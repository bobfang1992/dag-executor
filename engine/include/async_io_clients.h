#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "async_redis_client.h"
#include "endpoint_registry.h"
#include "event_loop.h"

namespace ranking {

/**
 * AsyncIoClients - per-request async client cache for IO operations.
 *
 * Similar to the synchronous IoClients, but for async clients that work
 * with the EventLoop. Each request execution owns an AsyncIoClients instance
 * that caches connected async clients for the lifetime of the request.
 *
 * Thread safety: This class is NOT thread-safe. All operations must be called
 * from the EventLoop thread. The design assumes a single event loop drives
 * all async operations within a request.
 *
 * Usage:
 *   AsyncIoClients clients;
 *   auto result = clients.GetRedis(loop, endpoints, "ep_0001");
 *   if (!result) { ... handle error ... }
 *   AsyncRedisClient& redis = **result;
 *   auto value = co_await redis.HGet("key", "field");
 */
class AsyncIoClients {
 public:
  AsyncIoClients() = default;
  ~AsyncIoClients() = default;

  // Non-copyable, non-movable (owns clients)
  AsyncIoClients(const AsyncIoClients&) = delete;
  AsyncIoClients& operator=(const AsyncIoClients&) = delete;
  AsyncIoClients(AsyncIoClients&&) = delete;
  AsyncIoClients& operator=(AsyncIoClients&&) = delete;

  /**
   * Get or create an async Redis client for the given endpoint.
   *
   * MUST be called on the EventLoop thread.
   *
   * @param loop EventLoop to use for async operations
   * @param endpoints EndpointRegistry for configuration lookup
   * @param endpoint_id The endpoint ID (e.g., "ep_0001")
   * @return Pointer to client on success, or error message
   */
  std::expected<AsyncRedisClient*, std::string> GetRedis(EventLoop& loop,
                                                          const rankd::EndpointRegistry& endpoints,
                                                          std::string_view endpoint_id);

  /**
   * Get an existing async Redis client (no creation).
   *
   * @param endpoint_id The endpoint ID
   * @return Pointer to client if exists, nullptr otherwise
   */
  AsyncRedisClient* GetExistingRedis(std::string_view endpoint_id);

  /**
   * Clear all cached clients.
   *
   * Useful for cleanup or reconnection scenarios.
   */
  void Clear();

  // Number of cached Redis clients
  size_t redis_count() const { return redis_clients_.size(); }

 private:
  std::unordered_map<std::string, std::unique_ptr<AsyncRedisClient>> redis_clients_;
};

}  // namespace ranking
