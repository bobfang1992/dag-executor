#include "pred_eval.h"
#include "task_registry.h"
#include <stdexcept>

namespace rankd {

class FilterTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "filter",
        .params_schema =
            {
                {.name = "pred_id",
                 .type = TaskParamType::PredId,
                 .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 50},
        .output_pattern = OutputPattern::StableFilter,
        // writes_effect omitted - no column writes
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params, const ExecCtx &ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("filter: expected exactly 1 input");
    }

    const std::string &pred_id = params.get_string("pred_id");
    if (pred_id.empty()) {
      throw std::runtime_error("filter: 'pred_id' must be non-empty");
    }

    // pred_id must exist in pred_table
    if (!ctx.pred_table) {
      throw std::runtime_error("filter: no pred_table in context");
    }
    auto pred_it = ctx.pred_table->find(pred_id);
    if (pred_it == ctx.pred_table->end()) {
      throw std::runtime_error("filter: pred_id '" + pred_id +
                               "' not found in pred_table");
    }
    const PredNode &pred = *pred_it->second;

    const auto &input = inputs[0];

    // Build new selection by filtering active rows
    SelectionVector new_selection;
    input.activeRows().forEachIndex([&](RowIndex idx) {
      if (eval_pred(pred, idx, input.batch(), ctx)) {
        new_selection.push_back(idx);
      }
    });

    // Return new RowSet with same batch, updated selection
    return input.withSelectionClearOrder(std::move(new_selection));
  }
};

// Auto-register this task with namespace
REGISTER_TASK(FilterTask);

} // namespace rankd
