#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "redis_client.h"

namespace rankd {

// Forward declarations
class EndpointRegistry;
struct ExecCtx;

/**
 * IoClients - per-request client cache for IO operations.
 *
 * Each request execution owns an IoClients instance that caches
 * connected clients (Redis, etc.) for the lifetime of the request.
 * This avoids creating a new connection per task invocation.
 *
 * Thread safety: Not thread-safe. Each request should have its own instance.
 */
struct IoClients {
  // Cache of Redis clients by endpoint_id
  // Clients are created on first use and reused for the request lifetime
  std::unordered_map<std::string, std::unique_ptr<RedisClient>> redis_by_endpoint;

  // Destructor cleans up all clients
  ~IoClients();

  // Non-copyable (owns unique_ptrs)
  IoClients() = default;
  IoClients(const IoClients&) = delete;
  IoClients& operator=(const IoClients&) = delete;
  IoClients(IoClients&&) = default;
  IoClients& operator=(IoClients&&) = default;
};

/**
 * Get or create a Redis client for the given endpoint.
 *
 * On first use of an endpoint within a request:
 *   - Resolves host/port from EndpointRegistry
 *   - Creates and connects the client
 *   - Caches it in ctx.clients->redis_by_endpoint
 *
 * Subsequent calls with the same endpoint_id reuse the cached client.
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
RedisClient& GetRedisClient(const ExecCtx& ctx, std::string_view endpoint_id);

}  // namespace rankd
