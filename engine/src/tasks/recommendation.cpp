#include "endpoint_registry.h"
#include "param_table.h"
#include "redis_client.h"
#include "request.h"
#include "task_registry.h"
#include <charconv>
#include <stdexcept>

namespace rankd {

// Recommendation task: reads recommendation:{user_id} LIST from Redis
// Returns fanout rows with recommendation IDs
class RecommendationTask {
 public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "recommendation",
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
      throw std::runtime_error("recommendation: expected 0 inputs");
    }

    // Get user_id from request context
    if (!ctx.request) {
      throw std::runtime_error("recommendation: missing request context");
    }
    uint32_t user_id = ctx.request->user_id;

    // Get fanout
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error("recommendation: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000'000;
    if (fanout > kMaxFanout) {
      throw std::runtime_error(
          "recommendation: 'fanout' exceeds maximum limit (10000000)");
    }

    // Get endpoint
    if (!ctx.endpoints) {
      throw std::runtime_error("recommendation: missing endpoint registry");
    }
    const std::string& endpoint_id = params.get_string("endpoint");
    const EndpointSpec* endpoint = ctx.endpoints->by_id(endpoint_id);
    if (!endpoint) {
      throw std::runtime_error("recommendation: unknown endpoint: " +
                               endpoint_id);
    }

    // Create Redis client and fetch recommendation list
    RedisClient redis(*endpoint);
    std::string key = "recommendation:" + std::to_string(user_id);
    auto result = redis.lrange(key, 0, fanout - 1);

    if (!result) {
      throw std::runtime_error("recommendation: " + result.error());
    }

    const auto& rec_ids_str = result.value();

    // Create batch with recommendation IDs
    size_t n = rec_ids_str.size();
    auto batch = std::make_shared<ColumnBatch>(n);

    for (size_t i = 0; i < n; ++i) {
      // Parse recommendation ID from string
      int64_t id = 0;
      auto [ptr, ec] = std::from_chars(rec_ids_str[i].data(),
                                       rec_ids_str[i].data() + rec_ids_str[i].size(), id);
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
static TaskRegistrar<RecommendationTask> registrar;

}  // namespace rankd
