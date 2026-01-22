#include "task_registry.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace rankd {

class SleepTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "sleep",
        .params_schema =
            {
                {.name = "duration_ms",
                 .type = TaskParamType::Int,
                 .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 10000},  // Allow up to 10s sleep
        .output_pattern = OutputPattern::UnaryPreserveView,
        // writes_effect omitted - identity transform
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params,
                    [[maybe_unused]] const ExecCtx &ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("sleep: expected exactly 1 input");
    }
    int64_t duration_ms = params.get_int("duration_ms");
    if (duration_ms < 0) {
      throw std::runtime_error("sleep: 'duration_ms' must be >= 0");
    }

    // Sleep for the specified duration
    if (duration_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    }

    // Pass through input unchanged (identity operation)
    return inputs[0];
  }
};

// Auto-register this task
static TaskRegistrar<SleepTask> registrar;

}  // namespace rankd
