#include "task_registry.h"
#include <stdexcept>

namespace rankd {

class ViewerFetchCachedRecommendationTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "viewer.fetch_cached_recommendation",
        .params_schema =
            {
                {.name = "fanout", .type = TaskParamType::Int, .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 100},
        .output_pattern = OutputPattern::SourceFanoutDense,
        .writes_effect = EffectKeys{}, // Source task, no column writes
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params,
                    [[maybe_unused]] const ExecCtx &ctx) {
    if (!inputs.empty()) {
      throw std::runtime_error(
          "viewer.fetch_cached_recommendation: expected 0 inputs");
    }
    int64_t fanout = params.get_int("fanout");
    if (fanout <= 0) {
      throw std::runtime_error(
          "viewer.fetch_cached_recommendation: 'fanout' must be > 0");
    }
    constexpr int64_t kMaxFanout = 10'000'000;
    if (fanout > kMaxFanout) {
      throw std::runtime_error("viewer.fetch_cached_recommendation: "
                               "'fanout' exceeds maximum limit (10000000)");
    }

    size_t n = static_cast<size_t>(fanout);

    // Create ColumnBatch with ids 1001..1000+fanout
    auto batch = std::make_shared<ColumnBatch>(n);
    for (size_t i = 0; i < n; ++i) {
      batch->setId(i, 1001 + static_cast<int64_t>(i));
    }

    // Add country column: dict ["CA","FR"], pattern CA,FR,CA,FR...
    auto country_dict = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"CA", "FR"});
    auto country_codes = std::make_shared<std::vector<int32_t>>(n);
    auto country_valid = std::make_shared<std::vector<uint8_t>>(n, 1);
    for (size_t i = 0; i < n; ++i) {
      (*country_codes)[i] = static_cast<int32_t>(i % 2); // 0=CA, 1=FR
    }
    auto country_col = std::make_shared<StringDictColumn>(country_dict,
                                                          country_codes,
                                                          country_valid);
    *batch = batch->withStringColumn(3001, country_col);

    // No title column for this source

    return RowSet(std::make_shared<ColumnBatch>(*batch));
  }
};

// Auto-register this task
static TaskRegistrar<ViewerFetchCachedRecommendationTask> registrar;

} // namespace rankd
