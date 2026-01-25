#include "async_dag_scheduler.h"
#include "coro_task.h"
#include "task_registry.h"
#include "uv_sleep.h"
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
                // Fault injection: throw after sleeping (for testing fail-fast)
                {.name = "fail_after_sleep",
                 .type = TaskParamType::Bool,
                 .required = false,
                 .default_value = false},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 10000},  // Allow up to 10s sleep
        .output_pattern = OutputPattern::UnaryPreserveView,
        // writes_effect omitted - identity transform
        .run_async = run_async,
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

    // Fault injection for testing
    if (params.get_bool("fail_after_sleep")) {
      throw std::runtime_error("sleep: intentional failure (fail_after_sleep=true)");
    }

    // Pass through input unchanged (identity operation)
    return inputs[0];
  }

  // Async version using libuv timer
  static ranking::Task<RowSet> run_async(const std::vector<RowSet>& inputs,
                                          const ValidatedParams& params,
                                          const ranking::ExecCtxAsync& ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("sleep: expected exactly 1 input");
    }
    int64_t duration_ms = params.get_int("duration_ms");
    if (duration_ms < 0) {
      throw std::runtime_error("sleep: 'duration_ms' must be >= 0");
    }

    // Async sleep using libuv timer
    if (duration_ms > 0) {
      co_await ranking::SleepMs(*ctx.loop, static_cast<uint64_t>(duration_ms));
    }

    // Fault injection for testing (throws AFTER async sleep completes)
    if (params.get_bool("fail_after_sleep")) {
      throw std::runtime_error("sleep: intentional failure (fail_after_sleep=true)");
    }

    // Pass through input unchanged (identity operation)
    co_return inputs[0];
  }
};

// Auto-register this task with namespace
REGISTER_TASK(SleepTask);

}  // namespace rankd
