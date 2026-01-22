#include "io_clients.h"

#include "endpoint_registry.h"
#include "param_table.h"
#include "redis_client.h"
#include <stdexcept>

namespace rankd {

// Destructor - unique_ptrs clean up automatically
IoClients::~IoClients() = default;

RedisClient& IoClients::getRedis(const EndpointRegistry& endpoints,
                                  std::string_view endpoint_id) {
  std::string endpoint_key(endpoint_id);

  // Fast path: check cache with lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = redis_by_endpoint_.find(endpoint_key);
    if (it != redis_by_endpoint_.end()) {
      return *it->second;
    }
  }

  // Slow path: resolve endpoint and create client
  // We hold the lock during creation for simplicity (MVP)
  // Connection happens lazily on first Redis command anyway
  const EndpointSpec* endpoint = endpoints.by_id(endpoint_key);
  if (!endpoint) {
    throw std::runtime_error(
        "IoClients::getRedis: unknown endpoint: " + endpoint_key);
  }

  // Fail-closed: must be a Redis endpoint
  if (endpoint->kind != EndpointKind::Redis) {
    throw std::runtime_error(
        "IoClients::getRedis: endpoint '" + endpoint_key + "' is not a Redis endpoint");
  }

  // Create client and cache it
  std::lock_guard<std::mutex> lock(mutex_);

  // Double-check: another thread may have created it while we were outside the lock
  auto it = redis_by_endpoint_.find(endpoint_key);
  if (it != redis_by_endpoint_.end()) {
    return *it->second;
  }

  auto client = std::make_unique<RedisClient>(*endpoint);
  auto& ref = *client;
  redis_by_endpoint_[endpoint_key] = std::move(client);

  return ref;
}

}  // namespace rankd
