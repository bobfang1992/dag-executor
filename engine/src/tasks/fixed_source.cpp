#include "async_dag_scheduler.h"
#include "coro_task.h"
#include "task_registry.h"
#include <stdexcept>

namespace rankd {

/**
 * FixedSource task: returns a deterministic RowSet with no inputs.
 *
 * This is a pure source task used for testing deadline/timeout behavior
 * without requiring Redis or any external dependencies.
 *
 * - 0 inputs, returns deterministic RowSet
 * - Has run_async that immediately co_returns
 * - Params: row_count (optional, default 1)
 * - No IO, no endpoint, no busy_wait
 */
class FixedSourceTask {
 public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "fixed_source",
        .params_schema =
            {
                {.name = "row_count",
                 .type = TaskParamType::Int,
                 .required = false,
                 .nullable = false,
                 .default_value = static_cast<int64_t>(1)},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},  // Just returns IDs, no data columns
        .default_budget = {.timeout_ms = 100},
        .output_pattern = OutputPattern::VariableDense,
        .writes_effect = std::nullopt,
        .is_io = false,
        .run_async = run_async,
    };
  }

  static RowSet run(const std::vector<RowSet>& inputs,
                    const ValidatedParams& params,
                    [[maybe_unused]] const ExecCtx& ctx) {
    if (!inputs.empty()) {
      throw std::runtime_error("fixed_source: expected 0 inputs");
    }

    int64_t row_count = params.get_int("row_count");
    if (row_count < 0) {
      throw std::runtime_error("fixed_source: 'row_count' must be >= 0");
    }

    // Create batch with deterministic IDs
    auto batch = std::make_shared<ColumnBatch>(static_cast<size_t>(row_count));
    for (size_t i = 0; i < static_cast<size_t>(row_count); ++i) {
      batch->setId(i, static_cast<int64_t>(i + 1));  // IDs: 1, 2, 3, ...
    }

    return RowSet(batch);
  }

  // Async version - immediately returns (no IO, no busy wait)
  static ranking::Task<RowSet> run_async(const std::vector<RowSet>& inputs,
                                          const ValidatedParams& params,
                                          [[maybe_unused]] const ranking::ExecCtxAsync& ctx) {
    if (!inputs.empty()) {
      throw std::runtime_error("fixed_source: expected 0 inputs");
    }

    int64_t row_count = params.get_int("row_count");
    if (row_count < 0) {
      throw std::runtime_error("fixed_source: 'row_count' must be >= 0");
    }

    // Create batch with deterministic IDs
    auto batch = std::make_shared<ColumnBatch>(static_cast<size_t>(row_count));
    for (size_t i = 0; i < static_cast<size_t>(row_count); ++i) {
      batch->setId(i, static_cast<int64_t>(i + 1));  // IDs: 1, 2, 3, ...
    }

    co_return RowSet(batch);
  }
};

// Auto-register this task
static TaskRegistrar<FixedSourceTask> registrar;

}  // namespace rankd
