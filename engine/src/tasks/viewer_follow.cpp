#include "task_registry.h"
#include <stdexcept>

namespace rankd {

class ViewerFollowTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "viewer.follow",
        .params_schema =
            {
                {.name = "fanout", .type = TaskParamType::Int, .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {KeyId::country, KeyId::title},  // Fixed schema columns
        .default_budget = {.timeout_ms = 100},
        .output_pattern = OutputPattern::SourceFanoutDense,
        // writes_effect omitted - no param-dependent writes
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params,
                    [[maybe_unused]] const ExecCtx &ctx) {
    if (!inputs.empty()) {
      throw std::runtime_error("viewer.follow: expected 0 inputs");
    }
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error("viewer.follow: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000'000; // 10M limit
    if (fanout > kMaxFanout) {
      throw std::runtime_error(
          "viewer.follow: 'fanout' exceeds maximum limit (10000000)");
    }

    size_t n = static_cast<size_t>(fanout);

    // Create ColumnBatch with ids 1..fanout
    auto batch = std::make_shared<ColumnBatch>(n);
    for (size_t i = 0; i < n; ++i) {
      batch->setId(i, static_cast<int64_t>(i + 1)); // ids are 1-indexed
    }

    // Add country column: dict ["US","CA"], pattern US,CA,US,CA...
    auto country_dict = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"US", "CA"});
    auto country_codes = std::make_shared<std::vector<int32_t>>(n);
    auto country_valid = std::make_shared<std::vector<uint8_t>>(n, 1);
    for (size_t i = 0; i < n; ++i) {
      (*country_codes)[i] = static_cast<int32_t>(i % 2); // 0=US, 1=CA
    }
    auto country_col = std::make_shared<StringDictColumn>(country_dict,
                                                          country_codes,
                                                          country_valid);
    *batch = batch->withStringColumn(3001, country_col); // country key_id

    // Add title column: dict ["L1","L2",...,"L{fanout}"], codes 0..fanout-1
    auto title_dict = std::make_shared<std::vector<std::string>>();
    title_dict->reserve(n);
    for (size_t i = 0; i < n; ++i) {
      title_dict->push_back("L" + std::to_string(i + 1));
    }
    auto title_codes = std::make_shared<std::vector<int32_t>>(n);
    auto title_valid = std::make_shared<std::vector<uint8_t>>(n, 1);
    for (size_t i = 0; i < n; ++i) {
      (*title_codes)[i] = static_cast<int32_t>(i);
    }
    auto title_col =
        std::make_shared<StringDictColumn>(title_dict, title_codes, title_valid);
    *batch = batch->withStringColumn(3002, title_col); // title key_id

    return RowSet(std::make_shared<ColumnBatch>(*batch));
  }
};

// Auto-register this task
static TaskRegistrar<ViewerFollowTask> registrar;

} // namespace rankd
