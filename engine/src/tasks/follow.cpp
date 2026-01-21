#include "endpoint_registry.h"
#include "param_table.h"
#include "redis_client.h"
#include "request.h"
#include "task_registry.h"
#include <charconv>
#include <stdexcept>

namespace rankd {

// Follow task: reads follow:{user_id} LIST from Redis
// Returns fanout rows with followee IDs
class FollowTask {
 public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "follow",
        .params_schema =
            {
                {.name = "endpoint",
                 .type = TaskParamType::EndpointRef,
                 .required = true,
                 .nullable = false,
                 .default_value = std::nullopt,
                 .endpoint_kind = EndpointKind::Redis},
                {.name = "fanout",
                 .type = TaskParamType::Int,
                 .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},  // Only ID column
        .default_budget = {.timeout_ms = 100},
        .output_pattern = OutputPattern::VariableDense,
    };
  }

  static RowSet run(const std::vector<RowSet>& inputs,
                    const ValidatedParams& params,
                    const ExecCtx& ctx) {
    if (!inputs.empty()) {
      throw std::runtime_error("follow: expected 0 inputs");
    }

    // Get user_id from request context
    if (!ctx.request) {
      throw std::runtime_error("follow: missing request context");
    }
    uint32_t user_id = ctx.request->user_id;

    // Get fanout
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error("follow: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000'000;
    if (fanout > kMaxFanout) {
      throw std::runtime_error(
          "follow: 'fanout' exceeds maximum limit (10000000)");
    }

    // Get endpoint
    if (!ctx.endpoints) {
      throw std::runtime_error("follow: missing endpoint registry");
    }
    const std::string& endpoint_id = params.get_string("endpoint");
    const EndpointSpec* endpoint = ctx.endpoints->by_id(endpoint_id);
    if (!endpoint) {
      throw std::runtime_error("follow: unknown endpoint: " + endpoint_id);
    }

    // Create Redis client and fetch follow list
    RedisClient redis(*endpoint);
    std::string key = "follow:" + std::to_string(user_id);
    auto result = redis.lrange(key, 0, fanout - 1);

    if (!result) {
      throw std::runtime_error("follow: " + result.error());
    }

    const auto& followees = result.value();

    // Create batch with followee IDs
    size_t n = followees.size();
    auto batch = std::make_shared<ColumnBatch>(n);

    for (size_t i = 0; i < n; ++i) {
      // Parse followee ID from string
      int64_t id = 0;
      auto [ptr, ec] = std::from_chars(
          followees[i].data(), followees[i].data() + followees[i].size(), id);
      if (ec != std::errc{}) {
        // Invalid ID string, use 0
        id = 0;
      }
      batch->setId(i, id);
    }

    return RowSet(std::make_shared<ColumnBatch>(*batch));
  }
};

// Auto-register this task
static TaskRegistrar<FollowTask> registrar;

}  // namespace rankd
