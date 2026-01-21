#include "endpoint_registry.h"
#include "param_table.h"
#include "redis_client.h"
#include "request.h"
#include "task_registry.h"
#include <charconv>
#include <stdexcept>

namespace rankd {

// Media task: reads media:{row.id} LIST from Redis for each input row
// Returns N×fanout rows with media IDs
class MediaTask {
 public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "media",
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
      throw std::runtime_error("media: expected exactly 1 input");
    }

    // Get fanout
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error("media: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000;  // Per-row limit
    if (fanout > kMaxFanout) {
      throw std::runtime_error(
          "media: 'fanout' exceeds per-row limit (10000)");
    }

    // Get endpoint
    if (!ctx.endpoints) {
      throw std::runtime_error("media: missing endpoint registry");
    }
    const std::string& endpoint_id = params.get_string("endpoint");
    const EndpointSpec* endpoint = ctx.endpoints->by_id(endpoint_id);
    if (!endpoint) {
      throw std::runtime_error("media: unknown endpoint: " + endpoint_id);
    }

    // Create Redis client
    RedisClient redis(*endpoint);

    // Collect all media IDs
    std::vector<int64_t> media_ids;
    const auto& input = inputs[0];

    input.activeRows().forEachIndex([&](RowIndex idx) {
      int64_t row_id = input.batch().getId(idx);

      // Fetch media list for this row's ID
      std::string key = "media:" + std::to_string(row_id);
      auto result = redis.lrange(key, 0, fanout - 1);

      if (result) {
        for (const auto& id_str : result.value()) {
          int64_t media_id = 0;
          auto [ptr, ec] = std::from_chars(
              id_str.data(), id_str.data() + id_str.size(), media_id);
          if (ec == std::errc{}) {
            media_ids.push_back(media_id);
          }
        }
      }
      // On error, just skip this row's media (fail-soft for fanout)
    });

    // Create batch with media IDs
    size_t n = media_ids.size();
    auto batch = std::make_shared<ColumnBatch>(n);

    for (size_t i = 0; i < n; ++i) {
      batch->setId(i, media_ids[i]);
    }

    return RowSet(std::make_shared<ColumnBatch>(*batch));
  }
};

// Auto-register this task
static TaskRegistrar<MediaTask> registrar;

}  // namespace rankd
