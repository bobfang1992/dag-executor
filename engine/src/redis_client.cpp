#include "redis_client.h"
#include <hiredis.h>

namespace rankd {

RedisClient::RedisClient(const EndpointSpec& endpoint)
    : host_(endpoint.static_resolver.host),
      port_(endpoint.static_resolver.port),
      connect_timeout_ms_(endpoint.policy.connect_timeout_ms.value_or(50)),
      request_timeout_ms_(endpoint.policy.request_timeout_ms.value_or(20)) {}

RedisClient::~RedisClient() { disconnect(); }

RedisClient::RedisClient(RedisClient&& other) noexcept
    : host_(std::move(other.host_)),
      port_(other.port_),
      connect_timeout_ms_(other.connect_timeout_ms_),
      request_timeout_ms_(other.request_timeout_ms_),
      ctx_(other.ctx_),
      last_error_(std::move(other.last_error_)) {
  other.ctx_ = nullptr;
}

RedisClient& RedisClient::operator=(RedisClient&& other) noexcept {
  if (this != &other) {
    disconnect();
    host_ = std::move(other.host_);
    port_ = other.port_;
    connect_timeout_ms_ = other.connect_timeout_ms_;
    request_timeout_ms_ = other.request_timeout_ms_;
    ctx_ = other.ctx_;
    last_error_ = std::move(other.last_error_);
    other.ctx_ = nullptr;
  }
  return *this;
}

void RedisClient::disconnect() {
  if (ctx_) {
    redisFree(ctx_);
    ctx_ = nullptr;
  }
}

std::expected<void, std::string> RedisClient::ensure_connected() {
  if (ctx_ != nullptr) {
    return {};  // Already connected
  }

  // Convert timeout to timeval
  struct timeval tv;
  tv.tv_sec = connect_timeout_ms_ / 1000;
  tv.tv_usec = (connect_timeout_ms_ % 1000) * 1000;

  ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);

  if (ctx_ == nullptr) {
    last_error_ = "redis: failed to allocate context";
    return std::unexpected(last_error_);
  }

  if (ctx_->err) {
    last_error_ =
        "redis: connect failed: " + std::string(ctx_->errstr);
    redisFree(ctx_);
    ctx_ = nullptr;
    return std::unexpected(last_error_);
  }

  // Set command timeout
  struct timeval cmd_tv;
  cmd_tv.tv_sec = request_timeout_ms_ / 1000;
  cmd_tv.tv_usec = (request_timeout_ms_ % 1000) * 1000;
  if (redisSetTimeout(ctx_, cmd_tv) != REDIS_OK) {
    last_error_ = "redis: failed to set timeout";
    redisFree(ctx_);
    ctx_ = nullptr;
    return std::unexpected(last_error_);
  }

  return {};
}

std::expected<std::vector<std::string>, std::string> RedisClient::lrange(
    const std::string& key, int64_t start, int64_t stop) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto conn_result = ensure_connected();
  if (!conn_result) {
    return std::unexpected(conn_result.error());
  }

  redisReply* reply = static_cast<redisReply*>(
      redisCommand(ctx_, "LRANGE %s %lld %lld", key.c_str(), start, stop));

  if (reply == nullptr) {
    last_error_ = "redis: LRANGE failed: " + std::string(ctx_->errstr);
    disconnect();  // Connection may be broken
    return std::unexpected(last_error_);
  }

  // Handle error reply
  if (reply->type == REDIS_REPLY_ERROR) {
    last_error_ = "redis: LRANGE error: " + std::string(reply->str);
    freeReplyObject(reply);
    return std::unexpected(last_error_);
  }

  // Expect array reply
  if (reply->type != REDIS_REPLY_ARRAY) {
    last_error_ = "redis: LRANGE unexpected reply type: " +
                  std::to_string(reply->type);
    freeReplyObject(reply);
    return std::unexpected(last_error_);
  }

  std::vector<std::string> result;
  result.reserve(reply->elements);

  for (size_t i = 0; i < reply->elements; ++i) {
    redisReply* elem = reply->element[i];
    if (elem->type == REDIS_REPLY_STRING || elem->type == REDIS_REPLY_STATUS) {
      result.emplace_back(elem->str, elem->len);
    } else if (elem->type == REDIS_REPLY_NIL) {
      result.emplace_back("");  // nil -> empty string
    } else {
      // Unexpected element type, skip or treat as empty
      result.emplace_back("");
    }
  }

  freeReplyObject(reply);
  return result;
}

std::expected<std::unordered_map<std::string, std::string>, std::string>
RedisClient::hgetall(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto conn_result = ensure_connected();
  if (!conn_result) {
    return std::unexpected(conn_result.error());
  }

  redisReply* reply =
      static_cast<redisReply*>(redisCommand(ctx_, "HGETALL %s", key.c_str()));

  if (reply == nullptr) {
    last_error_ = "redis: HGETALL failed: " + std::string(ctx_->errstr);
    disconnect();  // Connection may be broken
    return std::unexpected(last_error_);
  }

  // Handle error reply
  if (reply->type == REDIS_REPLY_ERROR) {
    last_error_ = "redis: HGETALL error: " + std::string(reply->str);
    freeReplyObject(reply);
    return std::unexpected(last_error_);
  }

  // Expect array reply (field, value, field, value, ...)
  if (reply->type != REDIS_REPLY_ARRAY) {
    last_error_ = "redis: HGETALL unexpected reply type: " +
                  std::to_string(reply->type);
    freeReplyObject(reply);
    return std::unexpected(last_error_);
  }

  // Must have even number of elements
  if (reply->elements % 2 != 0) {
    last_error_ = "redis: HGETALL odd number of elements";
    freeReplyObject(reply);
    return std::unexpected(last_error_);
  }

  std::unordered_map<std::string, std::string> result;

  for (size_t i = 0; i < reply->elements; i += 2) {
    redisReply* field = reply->element[i];
    redisReply* value = reply->element[i + 1];

    std::string field_str;
    std::string value_str;

    if (field->type == REDIS_REPLY_STRING || field->type == REDIS_REPLY_STATUS) {
      field_str = std::string(field->str, field->len);
    }

    if (value->type == REDIS_REPLY_STRING || value->type == REDIS_REPLY_STATUS) {
      value_str = std::string(value->str, value->len);
    }

    result[field_str] = value_str;
  }

  freeReplyObject(reply);
  return result;
}

}  // namespace rankd
