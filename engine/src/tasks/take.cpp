#include "task_registry.h"
#include <stdexcept>

namespace rankd {

class TakeTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "take",
        .params_schema =
            {
                {.name = "count", .type = TaskParamType::Int, .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 10},
        .output_pattern = OutputPattern::PrefixOfInput,
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params,
                    [[maybe_unused]] const ExecCtx &ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("take: expected exactly 1 input");
    }
    int64_t count = params.get_int("count");
    if (count <= 0) {
      throw std::runtime_error("take: 'count' must be > 0");
    }

    const auto &input = inputs[0];
    size_t limit = static_cast<size_t>(count);

    // Use truncateTo - shares batch pointer, creates new selection
    return input.truncateTo(limit);
  }
};

// Auto-register this task
static TaskRegistrar<TakeTask> registrar;

} // namespace rankd
