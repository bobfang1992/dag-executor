#include "task_registry.h"
#include <chrono>
#include <stdexcept>

namespace rankd {

/**
 * BusyCpu task: spins CPU for a specified duration (for timeout testing).
 *
 * This task is designed to test OffloadCpuWithTimeout behavior:
 * - 1 input (pass-through)
 * - NO run_async -> goes through OffloadCpu path in async scheduler
 * - Params: busy_wait_ms - spins CPU for that duration
 *
 * Use this to verify that OffloadCpuWithTimeout properly times out
 * CPU-bound work that exceeds the deadline.
 */
class BusyCpuTask {
 public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "busy_cpu",
        .params_schema =
            {
                {.name = "busy_wait_ms",
                 .type = TaskParamType::Int,
                 .required = true,
                 .nullable = false},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 10000},  // Allow up to 10s busy wait
        .output_pattern = OutputPattern::UnaryPreserveView,
        .writes_effect = std::nullopt,
        .is_io = false,
        // NOTE: No run_async - this forces async scheduler to use OffloadCpu
    };
  }

  static RowSet run(const std::vector<RowSet>& inputs,
                    const ValidatedParams& params,
                    [[maybe_unused]] const ExecCtx& ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("busy_cpu: expected exactly 1 input");
    }

    int64_t busy_wait_ms = params.get_int("busy_wait_ms");
    if (busy_wait_ms < 0) {
      throw std::runtime_error("busy_cpu: 'busy_wait_ms' must be >= 0");
    }

    // Busy-wait (spin) to simulate CPU-bound work
    // Using sleep_for would not test CPU offload timeout properly
    if (busy_wait_ms > 0) {
      auto end_time = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(busy_wait_ms);
      while (std::chrono::steady_clock::now() < end_time) {
        // Spin
      }
    }

    // Pass through input unchanged (identity operation)
    return inputs[0];
  }
};

// Auto-register this task with namespace
REGISTER_TASK(BusyCpuTask);

}  // namespace rankd
