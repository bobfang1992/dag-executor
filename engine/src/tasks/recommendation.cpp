#include "endpoint_registry.h"
#include "io_clients.h"
#include "param_table.h"
#include "redis_client.h"
#include "task_registry.h"
#include <charconv>
#include <stdexcept>

namespace rankd {

// Recommendation task: fan-out transform that fetches recommendations for each input user
// Input: rows with user IDs
// Output: for each input user, up to 'fanout' recommendation rows
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
    if (inputs.size() != 1) {
      throw std::runtime_error("recommendation: expected 1 input, got " +
                               std::to_string(inputs.size()));
    }

    const RowSet& input = inputs[0];

    // Get fanout (per input user)
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error("recommendation: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000'000;
    if (fanout > kMaxFanout) {
      throw std::runtime_error(
          "recommendation: 'fanout' exceeds maximum limit (10000000)");
    }

    // Get Redis client from per-request cache
    const std::string& endpoint_id = params.get_string("endpoint");
    RedisClient& redis = GetRedisClient(ctx, endpoint_id);

    // Materialize input indices
    auto input_indices = input.materializeIndexViewForOutput(input.batch().size());

    // Collect all recommendation IDs
    std::vector<int64_t> all_recs;

    for (uint32_t idx : input_indices) {
      int64_t user_id = input.batch().getId(idx);

      // Fetch recommendation list for this user
      std::string key = "recommendation:" + std::to_string(user_id);
      auto result = redis.lrange(key, 0, fanout - 1);

      if (!result) {
        throw std::runtime_error("recommendation: " + result.error());
      }

      // Parse and collect recommendation IDs
      for (const auto& rec_str : result.value()) {
        int64_t id = 0;
        auto [ptr, ec] = std::from_chars(
            rec_str.data(), rec_str.data() + rec_str.size(), id);
        if (ec == std::errc{}) {
          all_recs.push_back(id);
        }
        // Skip invalid IDs silently
      }
    }

    // Create batch with all recommendation IDs
    size_t n = all_recs.size();
    auto batch = std::make_shared<ColumnBatch>(n);

    for (size_t i = 0; i < n; ++i) {
      batch->setId(i, all_recs[i]);
    }

    return RowSet(batch);
  }
};

// Auto-register this task
static TaskRegistrar<RecommendationTask> registrar;

}  // namespace rankd
