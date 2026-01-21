#include "io_clients.h"

#include "endpoint_registry.h"
#include "param_table.h"
#include "redis_client.h"
#include <stdexcept>

namespace rankd {

// Destructor - unique_ptrs clean up automatically
IoClients::~IoClients() = default;

RedisClient& GetRedisClient(const ExecCtx& ctx, std::string_view endpoint_id) {
  // Fail-closed: require clients cache
  if (!ctx.clients) {
    throw std::runtime_error(
        "GetRedisClient: missing IoClients in ExecCtx");
  }

  // Fail-closed: require endpoint registry
  if (!ctx.endpoints) {
    throw std::runtime_error(
        "GetRedisClient: missing EndpointRegistry in ExecCtx");
  }

  std::string endpoint_key(endpoint_id);

  // Check cache first
  auto it = ctx.clients->redis_by_endpoint.find(endpoint_key);
  if (it != ctx.clients->redis_by_endpoint.end()) {
    return *it->second;
  }

  // Not cached - resolve endpoint and create client
  const EndpointSpec* endpoint = ctx.endpoints->by_id(endpoint_key);
  if (!endpoint) {
    throw std::runtime_error(
        "GetRedisClient: unknown endpoint: " + endpoint_key);
  }

  // Fail-closed: must be a Redis endpoint
  if (endpoint->kind != EndpointKind::Redis) {
    throw std::runtime_error(
        "GetRedisClient: endpoint '" + endpoint_key + "' is not a Redis endpoint");
  }

  // Create and cache the client
  auto client = std::make_unique<RedisClient>(*endpoint);
  auto& ref = *client;
  ctx.clients->redis_by_endpoint[endpoint_key] = std::move(client);

  return ref;
}

}  // namespace rankd
