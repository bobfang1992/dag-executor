#include "async_redis_client.h"

#include <async.h>
#include <hiredis.h>
// libuv adapter must be included AFTER async.h
#include <adapters/libuv.h>

#include <cassert>
#include <cstdarg>
#include <cstring>

namespace ranking {

namespace {

// Parsed Redis reply data - extracted in callback before hiredis frees the original
struct ParsedReply {
  int type = 0;
  std::string str_value;               // For string/error replies
  std::vector<std::string> array_vals;  // For array replies
  int64_t integer = 0;                 // For integer replies
};

// Deep-copy a redisReply into ParsedReply before hiredis frees it
ParsedReply parse_reply(redisReply* r) {
  ParsedReply p;
  if (!r) return p;

  p.type = r->type;

  switch (r->type) {
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_STATUS:
      if (r->str && r->len > 0) {
        p.str_value.assign(r->str, r->len);
      }
      break;

    case REDIS_REPLY_INTEGER:
      p.integer = r->integer;
      break;

    case REDIS_REPLY_ARRAY:
      p.array_vals.reserve(r->elements);
      for (size_t i = 0; i < r->elements; ++i) {
        if (r->element[i] && r->element[i]->type == REDIS_REPLY_STRING && r->element[i]->str) {
          p.array_vals.emplace_back(r->element[i]->str, r->element[i]->len);
        } else if (r->element[i] && r->element[i]->type == REDIS_REPLY_NIL) {
          p.array_vals.emplace_back();  // Empty string for nil
        } else {
          p.array_vals.emplace_back();  // Empty string for other types
        }
      }
      break;

    case REDIS_REPLY_NIL:
      // Nothing to extract
      break;
  }

  return p;
}

// State for a pending Redis command, used to communicate between callback and coroutine.
// Heap-allocated and shared between the awaitable and the callback.
struct CommandState {
  std::coroutine_handle<> handle;
  ParsedReply reply;
  std::string error;
  AsyncInflightLimiter::Guard permit;  // Released when state is destroyed

  static void OnReply(redisAsyncContext* c, void* reply_ptr, void* privdata) {
    auto* state = static_cast<CommandState*>(privdata);
    if (!state) return;

    if (reply_ptr) {
      // Deep-copy the reply data BEFORE hiredis frees it after this callback
      state->reply = parse_reply(static_cast<redisReply*>(reply_ptr));
    } else if (c && c->err) {
      state->error = c->errstr ? c->errstr : "Unknown error";
    } else {
      state->error = "Null reply";
    }

    // Resume the coroutine - the state will be read in await_resume
    // We're already on the loop thread since this is a hiredis callback
    auto h = state->handle;
    h.resume();
    // Note: state is owned by the awaitable, not deleted here
  }
};

// Awaitable for a single Redis command that suspends until reply arrives.
//
// IMPORTANT: The Task returned by HGet/LRange/HGetAll MUST NOT be destroyed while
// awaiting. Destroying a suspended coroutine while a Redis command is in flight
// causes undefined behavior (the callback will have a dangling pointer).
// In practice, this means: don't drop/reassign the Task until it completes.
class RedisCommandAwaitable {
 public:
  // Note: We store ctx_ptr (pointer to client's ctx_ member) instead of ctx value
  // to handle disconnection while waiting for permits - the client clears ctx_
  // on disconnect and we check it before issuing commands.
  RedisCommandAwaitable(EventLoop& loop, redisAsyncContext** ctx_ptr, AsyncInflightLimiter& limiter,
                        std::string command)
      : loop_(loop),
        ctx_ptr_(ctx_ptr),
        limiter_(limiter),
        command_(std::move(command)),
        state_(std::make_unique<CommandState>()) {}

  // Move-only (reference member prevents assignment)
  RedisCommandAwaitable(RedisCommandAwaitable&&) = default;
  RedisCommandAwaitable& operator=(RedisCommandAwaitable&&) = delete;

  bool await_ready() const noexcept { return false; }

  bool await_suspend(std::coroutine_handle<> h) {
    state_->handle = h;

    // Post the command execution to the event loop thread
    // Capture raw pointer - unique_ptr still owned by this object
    auto* state_ptr = state_.get();
    bool posted = loop_.Post([this, state_ptr]() { execute_on_loop(state_ptr); });

    if (!posted) {
      // Loop not running - don't suspend
      state_->error = "EventLoop not running";
      return false;
    }
    return true;
  }

  std::expected<ParsedReply, AsyncRedisClient::Error> await_resume() {
    if (!state_->error.empty()) {
      return std::unexpected(AsyncRedisClient::Error{state_->error, 0});
    }
    if (state_->reply.type == 0) {
      return std::unexpected(AsyncRedisClient::Error{"No reply from Redis", 0});
    }
    if (state_->reply.type == REDIS_REPLY_ERROR) {
      return std::unexpected(
          AsyncRedisClient::Error{state_->reply.str_value, REDIS_ERR_OTHER});
    }
    return std::move(state_->reply);
  }

 private:
  void execute_on_loop(CommandState* state_ptr) {
    // Try to acquire a permit synchronously
    if (limiter_.try_acquire()) {
      state_ptr->permit = AsyncInflightLimiter::Guard(&limiter_);
      issue_command(state_ptr);
      return;
    }

    // Need to wait for a permit - create a wrapper coroutine
    // We store the task in a struct that self-destructs after completion
    struct WaitAndExecute {
      RedisCommandAwaitable* self;
      CommandState* state;
      Task<void> task;

      Task<void> run() {
        auto guard = co_await self->limiter_.acquire();
        state->permit = std::move(guard);
        // SAFETY: Capture loop reference BEFORE issue_command. If issue_command
        // fails, it resumes the awaiter synchronously, which may destroy the
        // RedisCommandAwaitable (self) before we return. Accessing self->loop_
        // after that would be use-after-free.
        EventLoop& loop = self->loop_;
        self->issue_command(state);
        // Post cleanup to next event loop iteration - coroutine will be at
        // final_suspend by then and safe to destroy.
        // SAFETY: We MUST defer deletion because we're still inside the coroutine.
        // Deleting 'this' would destroy the Task member and call handle_.destroy()
        // on the running coroutine frame, which is undefined behavior.
        auto* to_delete = this;
        bool posted = loop.Post([to_delete]() { delete to_delete; });
        if (!posted) {
          // Loop stopped - can't post. Leak intentionally to avoid UB.
          // This only happens during shutdown when the loop is already stopped,
          // so the leak is acceptable (process is exiting anyway).
          (void)to_delete;  // Suppress unused warning
        }
      }
    };

    auto* waiter = new WaitAndExecute{this, state_ptr, Task<void>(nullptr)};
    waiter->task = waiter->run();
    waiter->task.start();
  }

  void issue_command(CommandState* state_ptr) {
    // Check if connection is still valid (may have disconnected while waiting)
    redisAsyncContext* ctx = *ctx_ptr_;
    if (!ctx) {
      state_ptr->error = "Connection closed while waiting for permit";
      state_ptr->handle.resume();
      return;
    }

    // Issue the async command
    int status = redisAsyncFormattedCommand(ctx, CommandState::OnReply, state_ptr,
                                            command_.c_str(), command_.size());

    if (status != REDIS_OK) {
      // Command failed to queue - resume with error
      state_ptr->error = ctx->errstr ? ctx->errstr : "Failed to queue Redis command";
      state_ptr->handle.resume();
    }
    // On success, OnReply will be called later and will resume the coroutine
  }

  EventLoop& loop_;
  redisAsyncContext** ctx_ptr_;  // Pointer to client's ctx_ member
  AsyncInflightLimiter& limiter_;
  std::string command_;
  std::unique_ptr<CommandState> state_;
};

// Helper to build Redis protocol command
std::string build_command(const std::vector<std::string_view>& args) {
  assert(!args.empty() && "Redis command requires at least one argument (the command name)");

  std::string cmd;
  cmd.reserve(256);

  // RESP array header
  cmd += "*";
  cmd += std::to_string(args.size());
  cmd += "\r\n";

  // Each argument as bulk string
  for (const auto& arg : args) {
    cmd += "$";
    cmd += std::to_string(arg.size());
    cmd += "\r\n";
    cmd += arg;
    cmd += "\r\n";
  }

  return cmd;
}

}  // namespace

// RedisOpState callback - not used in new design but kept for header compatibility
void RedisOpState::OnReply(redisAsyncContext* c, void* reply, void* privdata) {
  // Delegate to CommandState - this is a legacy compatibility shim
  CommandState::OnReply(c, reply, privdata);
}

// AsyncRedisClient implementation

AsyncRedisClient::AsyncRedisClient(EventLoop& loop, std::string endpoint_id, int max_inflight)
    : loop_(loop),
      limiter_(max_inflight > 0 ? static_cast<size_t>(max_inflight) : 64),
      endpoint_id_(std::move(endpoint_id)) {}

AsyncRedisClient::~AsyncRedisClient() {
  if (ctx_) {
    // SAFETY: Clear data pointer before disconnect to prevent callbacks from
    // dereferencing freed client memory (use-after-free prevention).
    // The OnConnect/OnDisconnect callbacks check for null data and early-return.
    ctx_->data = nullptr;
    // NOTE: redisAsyncDisconnect is async - it will trigger OnDisconnect callback
    // (which will early-return due to null data) and then hiredis frees the context.
    // We must NOT call redisAsyncFree here as that would double-free.
    redisAsyncDisconnect(ctx_);
    ctx_ = nullptr;
  }
}

std::expected<std::unique_ptr<AsyncRedisClient>, std::string> AsyncRedisClient::Create(
    EventLoop& loop, const rankd::EndpointSpec& spec) {
  // Validate endpoint kind
  if (spec.kind != rankd::EndpointKind::Redis) {
    return std::unexpected("Endpoint is not a Redis endpoint: " + spec.endpoint_id);
  }

  // Get configuration
  int max_inflight = spec.policy.max_inflight.value_or(64);
  int connect_timeout_ms = spec.policy.connect_timeout_ms.value_or(50);

  auto client =
      std::unique_ptr<AsyncRedisClient>(new AsyncRedisClient(loop, spec.endpoint_id, max_inflight));

  // Connect
  auto result =
      client->connect(spec.static_resolver.host, spec.static_resolver.port, connect_timeout_ms);
  if (!result) {
    return std::unexpected(result.error());
  }

  return client;
}

std::expected<void, std::string> AsyncRedisClient::connect(const std::string& host, int port,
                                                            int connect_timeout_ms) {
  // Create async context with timeout
  redisOptions options = {0};
  REDIS_OPTIONS_SET_TCP(&options, host.c_str(), port);

  // Set connect timeout
  struct timeval tv;
  tv.tv_sec = connect_timeout_ms / 1000;
  tv.tv_usec = (connect_timeout_ms % 1000) * 1000;
  options.connect_timeout = &tv;

  ctx_ = redisAsyncConnectWithOptions(&options);
  if (!ctx_) {
    last_error_ = "Failed to allocate async context";
    return std::unexpected(last_error_);
  }

  if (ctx_->err) {
    last_error_ = ctx_->errstr ? ctx_->errstr : "Unknown connection error";
    redisAsyncFree(ctx_);
    ctx_ = nullptr;
    return std::unexpected(last_error_);
  }

  // Store pointer to this client for callbacks
  ctx_->data = this;

  // Attach to libuv event loop
  if (redisLibuvAttach(ctx_, loop_.RawLoop()) != REDIS_OK) {
    last_error_ = "Failed to attach to libuv loop";
    redisAsyncFree(ctx_);
    ctx_ = nullptr;
    return std::unexpected(last_error_);
  }

  // Set callbacks
  redisAsyncSetConnectCallback(ctx_, OnConnect);
  redisAsyncSetDisconnectCallback(ctx_, OnDisconnect);

  return {};
}

void AsyncRedisClient::OnConnect(const redisAsyncContext* c, int status) {
  // SAFETY: c->data is set to 'this' in connect() and cleared to nullptr in destructor
  // before calling redisAsyncDisconnect. This null check prevents use-after-free.
  auto* client = static_cast<AsyncRedisClient*>(c->data);
  if (!client) return;

  if (status == REDIS_OK) {
    client->connected_ = true;
    client->last_error_.clear();
  } else {
    client->connected_ = false;
    client->last_error_ = c->errstr ? c->errstr : "Connection failed";
  }
}

void AsyncRedisClient::OnDisconnect(const redisAsyncContext* c, int status) {
  // SAFETY: See OnConnect comment about c->data pointer validity.
  auto* client = static_cast<AsyncRedisClient*>(c->data);
  if (!client) return;

  client->connected_ = false;
  if (status != REDIS_OK) {
    client->last_error_ = c->errstr ? c->errstr : "Disconnected with error";
  }
  // Don't free ctx_ here - hiredis does it automatically after this callback
  client->ctx_ = nullptr;
}

Task<AsyncRedisClient::Result<std::optional<std::string>>> AsyncRedisClient::HGet(
    std::string_view key, std::string_view field) {
  if (!ctx_) {
    co_return std::unexpected(Error{"Not connected", REDIS_ERR_OTHER});
  }

  // Build HGET command
  std::string cmd = build_command({"HGET", key, field});

  // Create awaitable and execute
  auto reply_result = co_await RedisCommandAwaitable(loop_, &ctx_, limiter_, std::move(cmd));

  if (!reply_result) {
    co_return std::unexpected(reply_result.error());
  }

  auto& reply = *reply_result;

  // Parse result
  std::optional<std::string> result;
  if (reply.type == REDIS_REPLY_STRING) {
    result = std::move(reply.str_value);
  } else if (reply.type == REDIS_REPLY_NIL) {
    // Field doesn't exist - return nullopt
  } else {
    co_return std::unexpected(
        Error{"Unexpected reply type for HGET: " + std::to_string(reply.type), REDIS_ERR_OTHER});
  }

  co_return result;
}

Task<AsyncRedisClient::Result<std::vector<std::string>>> AsyncRedisClient::LRange(
    std::string_view key, int64_t start, int64_t stop) {
  if (!ctx_) {
    co_return std::unexpected(Error{"Not connected", REDIS_ERR_OTHER});
  }

  // Build LRANGE command
  std::string cmd = build_command({"LRANGE", key, std::to_string(start), std::to_string(stop)});

  auto reply_result = co_await RedisCommandAwaitable(loop_, &ctx_, limiter_, std::move(cmd));

  if (!reply_result) {
    co_return std::unexpected(reply_result.error());
  }

  auto& reply = *reply_result;

  // Parse result
  if (reply.type != REDIS_REPLY_ARRAY) {
    co_return std::unexpected(
        Error{"Unexpected reply type for LRANGE: " + std::to_string(reply.type), REDIS_ERR_OTHER});
  }

  co_return std::move(reply.array_vals);
}

Task<AsyncRedisClient::Result<std::vector<std::string>>> AsyncRedisClient::HGetAll(
    std::string_view key) {
  if (!ctx_) {
    co_return std::unexpected(Error{"Not connected", REDIS_ERR_OTHER});
  }

  // Build HGETALL command
  std::string cmd = build_command({"HGETALL", key});

  auto reply_result = co_await RedisCommandAwaitable(loop_, &ctx_, limiter_, std::move(cmd));

  if (!reply_result) {
    co_return std::unexpected(reply_result.error());
  }

  auto& reply = *reply_result;

  // Parse result (array of alternating field/value)
  if (reply.type != REDIS_REPLY_ARRAY) {
    co_return std::unexpected(
        Error{"Unexpected reply type for HGETALL: " + std::to_string(reply.type), REDIS_ERR_OTHER});
  }

  co_return std::move(reply.array_vals);
}

}  // namespace ranking
