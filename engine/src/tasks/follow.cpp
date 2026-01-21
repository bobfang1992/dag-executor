#include "endpoint_registry.h"
#include "io_clients.h"
#include "param_table.h"
#include "redis_client.h"
#include "task_registry.h"
#include <charconv>
#include <stdexcept>

namespace rankd {

// Follow task: fan-out transform that fetches follows for each input user
// Input: rows with user IDs
// Output: for each input user, up to 'fanout' followee rows
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
    if (inputs.size() != 1) {
      throw std::runtime_error("follow: expected 1 input, got " +
                               std::to_string(inputs.size()));
    }

    const RowSet& input = inputs[0];

    // Get fanout (per input user)
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error("follow: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000'000;
    if (fanout > kMaxFanout) {
      throw std::runtime_error(
          "follow: 'fanout' exceeds maximum limit (10000000)");
    }

    // Get Redis client from per-request cache
    const std::string& endpoint_id = params.get_string("endpoint");
    RedisClient& redis = GetRedisClient(ctx, endpoint_id);

    // Materialize input indices
    auto input_indices = input.materializeIndexViewForOutput(input.batch().size());

    // Collect all followee IDs
    std::vector<int64_t> all_followees;

    for (uint32_t idx : input_indices) {
      int64_t user_id = input.batch().getId(idx);

      // Fetch follow list for this user
      std::string key = "follow:" + std::to_string(user_id);
      auto result = redis.lrange(key, 0, fanout - 1);

      if (!result) {
        throw std::runtime_error("follow: " + result.error());
      }

      // Parse and collect followee IDs
      for (const auto& followee_str : result.value()) {
        int64_t id = 0;
        auto [ptr, ec] = std::from_chars(
            followee_str.data(), followee_str.data() + followee_str.size(), id);
        if (ec == std::errc{}) {
          all_followees.push_back(id);
        }
        // Skip invalid IDs silently
      }
    }

    // Create batch with all followee IDs
    size_t n = all_followees.size();
    auto batch = std::make_shared<ColumnBatch>(n);

    for (size_t i = 0; i < n; ++i) {
      batch->setId(i, all_followees[i]);
    }

    return RowSet(batch);
  }
};

// Auto-register this task
static TaskRegistrar<FollowTask> registrar;

}  // namespace rankd
