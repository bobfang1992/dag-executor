#pragma once

#include "column_batch.h"
#include "key_registry.h"
#include "param_table.h"
#include "plan.h"
#include <cmath>
#include <optional>
#include <stdexcept>

namespace rankd {

// Lookup KeyMeta by key_id (linear scan, OK for small registry)
inline const KeyMeta *findKeyById(uint32_t key_id) {
  for (const auto &meta : kKeyRegistry) {
    if (meta.id == key_id) {
      return &meta;
    }
  }
  return nullptr;
}

// Expression evaluation result: nullopt = null, otherwise numeric value
using ExprResult = std::optional<double>;

// Evaluate an expression node for a specific row
// Returns nullopt for null, double for numeric result
// Throws on non-finite result
inline ExprResult eval_expr(const ExprNode &node, size_t row,
                            const ColumnBatch &batch, const ExecCtx &ctx) {
  if (node.op == "const_number") {
    return node.const_value;
  }

  if (node.op == "const_null") {
    return std::nullopt;
  }

  if (node.op == "key_ref") {
    // Special case: Key.id (key_id=1)
    if (node.key_id == 1) {
      if (!batch.isIdValid(row)) {
        return std::nullopt;
      }
      return static_cast<double>(batch.getId(row));
    }

    // Float column lookup
    const FloatColumn *col = batch.getFloatCol(node.key_id);
    if (!col || col->valid[row] == 0) {
      return std::nullopt;
    }
    return col->values[row];
  }

  if (node.op == "param_ref") {
    if (!ctx.params) {
      return std::nullopt;
    }

    // Look up param metadata
    const ParamMeta *meta = nullptr;
    for (const auto &m : kParamRegistry) {
      if (m.id == node.param_id) {
        meta = &m;
        break;
      }
    }
    if (!meta) {
      return std::nullopt;
    }

    ParamId pid = static_cast<ParamId>(node.param_id);
    if (!ctx.params->has(pid)) {
      return std::nullopt;
    }
    if (ctx.params->isNull(pid)) {
      return std::nullopt;
    }

    // Get value based on type
    if (meta->type == ParamType::Int) {
      auto val = ctx.params->getInt(pid);
      if (val) {
        return static_cast<double>(*val);
      }
    } else if (meta->type == ParamType::Float) {
      auto val = ctx.params->getFloat(pid);
      if (val) {
        return *val;
      }
    }
    return std::nullopt;
  }

  if (node.op == "add" || node.op == "sub" || node.op == "mul") {
    auto a_result = eval_expr(*node.a, row, batch, ctx);
    auto b_result = eval_expr(*node.b, row, batch, ctx);

    // Null propagation
    if (!a_result || !b_result) {
      return std::nullopt;
    }

    double result;
    if (node.op == "add") {
      result = *a_result + *b_result;
    } else if (node.op == "sub") {
      result = *a_result - *b_result;
    } else {
      result = *a_result * *b_result;
    }
    return result;
  }

  if (node.op == "neg") {
    auto x_result = eval_expr(*node.a, row, batch, ctx);
    if (!x_result) {
      return std::nullopt;
    }
    return -(*x_result);
  }

  if (node.op == "coalesce") {
    auto a_result = eval_expr(*node.a, row, batch, ctx);
    if (a_result) {
      return a_result;
    }
    return eval_expr(*node.b, row, batch, ctx);
  }

  throw std::runtime_error("Unknown expr op: " + node.op);
}

} // namespace rankd
