#include "async_io_clients.h"

namespace ranking {

std::expected<AsyncRedisClient*, std::string> AsyncIoClients::GetRedis(
    EventLoop& loop, const rankd::EndpointRegistry& endpoints, std::string_view endpoint_id) {
  // Check cache first
  auto it = redis_clients_.find(std::string(endpoint_id));
  if (it != redis_clients_.end()) {
    return it->second.get();
  }

  // Look up endpoint
  const auto* spec = endpoints.by_id(endpoint_id);
  if (!spec) {
    return std::unexpected("Unknown endpoint: " + std::string(endpoint_id));
  }

  // Validate endpoint kind
  if (spec->kind != rankd::EndpointKind::Redis) {
    return std::unexpected("Endpoint is not a Redis endpoint: " + std::string(endpoint_id));
  }

  // Create new client
  auto client_result = AsyncRedisClient::Create(loop, *spec);
  if (!client_result) {
    return std::unexpected(client_result.error());
  }

  // Cache and return
  auto* client = client_result->get();
  redis_clients_[std::string(endpoint_id)] = std::move(*client_result);
  return client;
}

AsyncRedisClient* AsyncIoClients::GetExistingRedis(std::string_view endpoint_id) {
  auto it = redis_clients_.find(std::string(endpoint_id));
  if (it != redis_clients_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void AsyncIoClients::Clear() { redis_clients_.clear(); }

}  // namespace ranking
