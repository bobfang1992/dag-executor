#pragma once

#include "column_batch.h"
#include "expr_eval.h"
#include "plan.h"
#include <optional>

namespace rankd {

// Three-valued predicate result: true, false, or unknown (nullopt)
// SQL-like null semantics:
// - cmp operations with null operands yield unknown (nullopt)
// - is_null/not_null always yield true/false (never unknown)
// - NOT unknown = unknown
// - true AND unknown = unknown, false AND unknown = false
// - true OR unknown = true, false OR unknown = unknown
// - in with null lhs yields unknown
using PredResult = std::optional<bool>;

// Internal evaluation returning three-valued result
inline PredResult eval_pred_impl(const PredNode &node, size_t row,
                                 const ColumnBatch &batch, const ExecCtx &ctx) {
  if (node.op == "const_bool") {
    return node.const_value;
  }

  if (node.op == "and") {
    PredResult a = eval_pred_impl(*node.pred_a, row, batch, ctx);
    // Short-circuit: if a is false, result is false regardless of b
    if (a.has_value() && !*a) {
      return false;
    }
    PredResult b = eval_pred_impl(*node.pred_b, row, batch, ctx);
    // Short-circuit: if b is false, result is false regardless of a
    if (b.has_value() && !*b) {
      return false;
    }
    // If either is unknown, result is unknown
    if (!a.has_value() || !b.has_value()) {
      return std::nullopt;
    }
    // Both are true
    return true;
  }

  if (node.op == "or") {
    PredResult a = eval_pred_impl(*node.pred_a, row, batch, ctx);
    // Short-circuit: if a is true, result is true regardless of b
    if (a.has_value() && *a) {
      return true;
    }
    PredResult b = eval_pred_impl(*node.pred_b, row, batch, ctx);
    // Short-circuit: if b is true, result is true regardless of a
    if (b.has_value() && *b) {
      return true;
    }
    // If either is unknown, result is unknown
    if (!a.has_value() || !b.has_value()) {
      return std::nullopt;
    }
    // Both are false
    return false;
  }

  if (node.op == "not") {
    PredResult inner = eval_pred_impl(*node.pred_a, row, batch, ctx);
    // NOT unknown = unknown
    if (!inner.has_value()) {
      return std::nullopt;
    }
    return !*inner;
  }

  if (node.op == "is_null") {
    ExprResult val = eval_expr(*node.value_a, row, batch, ctx);
    // is_null always returns definite true/false, never unknown
    return !val.has_value();
  }

  if (node.op == "not_null") {
    ExprResult val = eval_expr(*node.value_a, row, batch, ctx);
    // not_null always returns definite true/false, never unknown
    return val.has_value();
  }

  if (node.op == "cmp") {
    ExprResult a = eval_expr(*node.value_a, row, batch, ctx);
    ExprResult b = eval_expr(*node.value_b, row, batch, ctx);

    // Any comparison with null yields unknown
    if (!a || !b) {
      return std::nullopt;
    }

    double av = *a;
    double bv = *b;

    if (node.cmp_op == "==") {
      return av == bv;
    }
    if (node.cmp_op == "!=") {
      return av != bv;
    }
    if (node.cmp_op == "<") {
      return av < bv;
    }
    if (node.cmp_op == "<=") {
      return av <= bv;
    }
    if (node.cmp_op == ">") {
      return av > bv;
    }
    if (node.cmp_op == ">=") {
      return av >= bv;
    }

    throw std::runtime_error("Unknown cmp operator: " + node.cmp_op);
  }

  if (node.op == "in") {
    ExprResult lhs = eval_expr(*node.value_a, row, batch, ctx);

    // If lhs is null, in yields unknown
    if (!lhs) {
      return std::nullopt;
    }

    double val = *lhs;

    // Check if value is in the list
    for (double item : node.in_list) {
      if (val == item) {
        return true;
      }
    }
    return false;
  }

  throw std::runtime_error("Unknown pred op: " + node.op);
}

// Public evaluation: converts unknown to false for filter purposes
// In filter context, unknown/null means "don't include this row"
inline bool eval_pred(const PredNode &node, size_t row, const ColumnBatch &batch,
                      const ExecCtx &ctx) {
  PredResult result = eval_pred_impl(node, row, batch, ctx);
  // Unknown (nullopt) is treated as false in filter context
  return result.value_or(false);
}

} // namespace rankd
