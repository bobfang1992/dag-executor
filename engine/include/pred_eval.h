#pragma once

#include "column_batch.h"
#include "expr_eval.h"
#include "plan.h"
#include <optional>

namespace rankd {

// Three-valued predicate result: true, false, or unknown (nullopt)
// Null semantics (per spec):
// - cmp operations with null operands yield false (per spec, not unknown)
// - in with null lhs yields false (null is not a member of any literal list)
// - is_null/not_null always yield true/false (never unknown)
// - NOT, AND, OR use three-valued logic if operands are unknown
// Note: Since cmp/in return false (not unknown) for null, NOT/AND/OR will
// see false, e.g., not (x > 5) with null x returns not false = true
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

    // Per spec: any comparison with null yields false (not unknown)
    // This means not (x > 5) with null x returns true (row included)
    // Use is_null/not_null to explicitly test for null values
    if (!a || !b) {
      return false;
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

    // If lhs is null, in yields false (per spec: null is not a member of any list)
    // This is different from cmp which yields unknown - in is a membership test
    // where null definitively is not in the set of literal values
    if (!lhs) {
      return false;
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
