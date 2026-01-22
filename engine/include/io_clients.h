#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "endpoint_registry.h"
#include "inflight_limiter.h"
#include "param_table.h"  // For ExecCtx definition
#include "redis_client.h"

namespace rankd {

/**
 * IoClients - per-request client cache for IO operations.
 *
 * Each request execution owns an IoClients instance that caches
 * connected clients (Redis, etc.) for the lifetime of the request.
 * This avoids creating a new connection per task invocation.
 *
 * Thread safety: Thread-safe. Multiple nodes in a DAG may access
 * concurrently under Level 2 parallelism. Internal mutex protects
 * the client cache map.
 */
struct IoClients {
  // Get or create a Redis client for the given endpoint.
  // Thread-safe: uses internal mutex to protect cache access.
  RedisClient& getRedis(const EndpointRegistry& endpoints, std::string_view endpoint_id);

  // Destructor cleans up all clients
  ~IoClients();

  // Non-copyable, non-movable (has mutex)
  IoClients() = default;
  IoClients(const IoClients&) = delete;
  IoClients& operator=(const IoClients&) = delete;
  IoClients(IoClients&&) = delete;
  IoClients& operator=(IoClients&&) = delete;

 private:
  // Cache of Redis clients by endpoint_id
  // Clients are created on first use and reused for the request lifetime
  std::unordered_map<std::string, std::unique_ptr<RedisClient>> redis_by_endpoint_;
  mutable std::mutex mutex_;
};

/**
 * Get or create a Redis client for the given endpoint.
 *
 * Convenience wrapper that calls ctx.clients->getRedis().
 *
 * Fail-closed behavior:
 *   - Throws if ctx.clients is null
 *   - Throws if ctx.endpoints is null
 *   - Throws if endpoint_id is unknown
 *   - Throws if endpoint kind is not Redis
 *
 * @param ctx Execution context (must have clients and endpoints set)
 * @param endpoint_id The endpoint ID (e.g., "ep_0001")
 * @return Reference to the cached Redis client
 * @throws std::runtime_error on any failure
 */
inline RedisClient& GetRedisClient(const ExecCtx& ctx, std::string_view endpoint_id) {
  if (!ctx.clients) {
    throw std::runtime_error("GetRedisClient: missing IoClients in ExecCtx");
  }
  if (!ctx.endpoints) {
    throw std::runtime_error("GetRedisClient: missing EndpointRegistry in ExecCtx");
  }
  return ctx.clients->getRedis(*ctx.endpoints, endpoint_id);
}

/**
 * Execute a Redis operation with inflight limiting.
 *
 * This helper:
 *   1. Gets (or creates) the Redis client for the endpoint
 *   2. Acquires an inflight slot (blocks if at limit)
 *   3. Executes the operation
 *   4. Releases the inflight slot on return
 *
 * Usage:
 *   auto result = WithInflightLimit(ctx, endpoint_id,
 *       [](RedisClient& redis) { return redis.hgetall("key"); });
 *
 * @param ctx Execution context
 * @param endpoint_id The endpoint ID (e.g., "ep_0001")
 * @param op Lambda that takes RedisClient& and returns the result
 * @return Result of the operation
 */
template <typename Op>
auto WithInflightLimit(const ExecCtx& ctx, std::string_view endpoint_id, Op&& op)
    -> decltype(op(std::declval<RedisClient&>())) {
  // Get the client (creates if needed)
  RedisClient& client = GetRedisClient(ctx, endpoint_id);

  // Get max_inflight from endpoint policy
  int max_inflight = kDefaultMaxInflight;
  if (ctx.endpoints) {
    if (const auto* ep = ctx.endpoints->by_id(endpoint_id)) {
      if (ep->policy.max_inflight) {
        max_inflight = *ep->policy.max_inflight;
      }
    }
  }

  // Acquire inflight slot (blocks if at limit, releases on scope exit)
  auto guard = InflightLimiter::acquire(std::string(endpoint_id), max_inflight);

  // Execute the operation
  return op(client);
}

}  // namespace rankd
