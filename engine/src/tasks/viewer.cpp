#include "endpoint_registry.h"
#include "io_clients.h"
#include "param_table.h"
#include "redis_client.h"
#include "request.h"
#include "task_registry.h"
#include <stdexcept>

namespace rankd {

// Viewer task: reads user:{user_id} HASH from Redis
// Returns single row with viewer's user data (country column)
class ViewerTask {
 public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "viewer",
        .params_schema =
            {
                {.name = "endpoint",
                 .type = TaskParamType::EndpointRef,
                 .required = true,
                 .nullable = false,
                 .default_value = std::nullopt,
                 .endpoint_kind = EndpointKind::Redis},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {KeyId::country},  // Writes country column
        .default_budget = {.timeout_ms = 100},
        .output_pattern = OutputPattern::VariableDense,
    };
  }

  static RowSet run(const std::vector<RowSet>& inputs,
                    const ValidatedParams& params,
                    const ExecCtx& ctx) {
    if (!inputs.empty()) {
      throw std::runtime_error("viewer: expected 0 inputs");
    }

    // Get user_id from request context
    if (!ctx.request) {
      throw std::runtime_error("viewer: missing request context");
    }
    uint32_t user_id = ctx.request->user_id;

    // Get endpoint and fetch user data with inflight limiting
    const std::string& endpoint_id = params.get_string("endpoint");
    std::string key = "user:" + std::to_string(user_id);

    auto result = WithInflightLimit(ctx, endpoint_id,
        [&key](RedisClient& redis) { return redis.hgetall(key); });

    if (!result) {
      throw std::runtime_error("viewer: " + result.error());
    }

    const auto& user_data = result.value();

    // Create single-row batch
    auto batch = std::make_shared<ColumnBatch>(1);
    batch->setId(0, static_cast<int64_t>(user_id));

    // Add country column
    auto country_it = user_data.find("country");
    if (country_it != user_data.end()) {
      auto country_dict =
          std::make_shared<std::vector<std::string>>(1, country_it->second);
      auto country_codes = std::make_shared<std::vector<int32_t>>(1, 0);
      auto country_valid = std::make_shared<std::vector<uint8_t>>(1, 1);
      auto country_col = std::make_shared<StringDictColumn>(
          country_dict, country_codes, country_valid);
      *batch = batch->withStringColumn(key_id(KeyId::country), country_col);
    } else {
      // No country data - create null column
      auto country_dict = std::make_shared<std::vector<std::string>>();
      auto country_codes = std::make_shared<std::vector<int32_t>>(1, -1);
      auto country_valid = std::make_shared<std::vector<uint8_t>>(1, 0);
      auto country_col = std::make_shared<StringDictColumn>(
          country_dict, country_codes, country_valid);
      *batch = batch->withStringColumn(key_id(KeyId::country), country_col);
    }

    return RowSet(std::make_shared<ColumnBatch>(*batch));
  }
};

// Auto-register this task
static TaskRegistrar<ViewerTask> registrar;

}  // namespace rankd
