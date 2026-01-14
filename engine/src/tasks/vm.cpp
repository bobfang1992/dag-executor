#include "expr_eval.h"
#include "task_registry.h"
#include <cmath>
#include <stdexcept>

namespace rankd {

class VmTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "vm",
        .params_schema =
            {
                {.name = "out_key", .type = TaskParamType::Int, .required = true},
                {.name = "expr_id",
                 .type = TaskParamType::ExprId,
                 .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 50},
        .output_pattern = OutputPattern::UnaryPreserveView,
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params, const ExecCtx &ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("vm: expected exactly 1 input");
    }

    int64_t out_key_raw = params.get_int("out_key");
    if (out_key_raw <= 0) {
      throw std::runtime_error("vm: 'out_key' must be > 0");
    }
    uint32_t out_key = static_cast<uint32_t>(out_key_raw);

    const std::string &expr_id = params.get_string("expr_id");
    if (expr_id.empty()) {
      throw std::runtime_error("vm: 'expr_id' must be non-empty");
    }

    // Validate out_key exists in key registry
    const KeyMeta *key_meta = findKeyById(out_key);
    if (!key_meta) {
      throw std::runtime_error("vm: out_key " + std::to_string(out_key) +
                               " not in key registry");
    }

    // out_key must NOT be Key.id (key_id=1)
    if (out_key == 1) {
      throw std::runtime_error("vm: cannot write to Key.id");
    }

    // out_key must have allow_write=true
    if (!key_meta->allow_write) {
      throw std::runtime_error("vm: key '" + std::string(key_meta->name) +
                               "' is not writable");
    }

    // out_key type must be float for this step
    if (key_meta->type != KeyType::Float) {
      throw std::runtime_error("vm: out_key must be Float type");
    }

    // expr_id must exist in expr_table
    if (!ctx.expr_table) {
      throw std::runtime_error("vm: no expr_table in context");
    }
    auto expr_it = ctx.expr_table->find(expr_id);
    if (expr_it == ctx.expr_table->end()) {
      throw std::runtime_error("vm: expr_id '" + expr_id +
                               "' not found in expr_table");
    }
    const ExprNode &expr = *expr_it->second;

    const auto &input = inputs[0];
    size_t n = input.batch().size();

    // Create new float column
    auto col = std::make_shared<FloatColumn>(n);

    // Evaluate expression for each active row
    bool has_null_active = false;
    input.activeRows().forEachIndex([&](RowIndex row) {
      ExprResult result = eval_expr(expr, row, input.batch(), ctx);

      if (!result) {
        has_null_active = true;
        // valid stays 0
      } else {
        double val = *result;
        // Check for non-finite
        if (!std::isfinite(val)) {
          throw std::runtime_error(
              "vm: expression produced non-finite value at row " +
              std::to_string(row));
        }
        col->values[row] = val;
        col->valid[row] = 1;
      }
    });

    // If out_key is not nullable and any active row is null => error
    if (!key_meta->nullable && has_null_active) {
      throw std::runtime_error("vm: null result for non-nullable key '" +
                               std::string(key_meta->name) + "'");
    }

    // Create new batch with the float column
    auto new_batch = std::make_shared<ColumnBatch>(
        input.batch().withFloatColumn(out_key, col));

    return input.withBatch(new_batch);
  }
};

// Auto-register this task
static TaskRegistrar<VmTask> registrar;

} // namespace rankd
