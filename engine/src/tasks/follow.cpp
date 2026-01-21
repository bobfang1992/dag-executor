#include "endpoint_registry.h"
#include "io_clients.h"
#include "key_registry.h"
#include "param_table.h"
#include "redis_client.h"
#include "task_registry.h"
#include <charconv>
#include <stdexcept>
#include <unordered_map>

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
        .writes = {KeyId::country},  // ID + country (hydrated)
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

    // Create batch with all followee IDs and hydrate country
    size_t n = all_followees.size();
    auto batch = std::make_shared<ColumnBatch>(n);

    // Build country column (dictionary-encoded strings)
    auto country_dict = std::make_shared<std::vector<std::string>>();
    auto country_codes = std::make_shared<std::vector<int32_t>>(n, -1);
    auto country_valid = std::make_shared<std::vector<uint8_t>>(n, 0);
    std::unordered_map<std::string, int32_t> country_to_code;

    for (size_t i = 0; i < n; ++i) {
      int64_t followee_id = all_followees[i];
      batch->setId(i, followee_id);

      // Fetch user data for this followee
      std::string user_key = "user:" + std::to_string(followee_id);
      auto user_result = redis.hgetall(user_key);
      if (!user_result) {
        // Fail on Redis errors (consistent with LRANGE above)
        throw std::runtime_error("follow: " + user_result.error());
      }
      // Empty result means user doesn't exist - leave country as null
      auto country_it = user_result.value().find("country");
      if (country_it != user_result.value().end()) {
        const std::string& country = country_it->second;
        auto it = country_to_code.find(country);
        if (it == country_to_code.end()) {
          int32_t code = static_cast<int32_t>(country_dict->size());
          country_dict->push_back(country);
          country_to_code[country] = code;
          (*country_codes)[i] = code;
        } else {
          (*country_codes)[i] = it->second;
        }
        (*country_valid)[i] = 1;
      }
      // If user not found or no country field, leave as null (valid=0, code=-1)
    }

    // Add country column
    auto country_col = std::make_shared<StringDictColumn>(
        country_dict, country_codes, country_valid);
    *batch = batch->withStringColumn(key_id(KeyId::country), country_col);

    return RowSet(std::make_shared<ColumnBatch>(*batch));
  }
};

// Auto-register this task
static TaskRegistrar<FollowTask> registrar;

}  // namespace rankd
