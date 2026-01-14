#pragma once

#include "column_batch.h"
#include "expr_eval.h"
#include "plan.h"
#include <optional>

namespace rankd {

// Three-valued predicate result: true, false, or unknown (nullopt)
// Null semantics (per spec §7.2):
// - == null / != null have explicit null semantics (like is_null/not_null)
//   - x == null returns true if x is null, false otherwise
//   - x != null returns true if x is not null, false otherwise
// - Other cmp operations (<, <=, >, >=) with null operands yield false
// - in with null lhs yields false (null is not a member of any literal list)
// - is_null/not_null always yield true/false (never unknown)
// - NOT, AND, OR use three-valued logic if operands are unknown
// Note: Since cmp (except ==,!=)/in return false for null, NOT/AND/OR will
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

    bool a_is_null = !a.has_value();
    bool b_is_null = !b.has_value();

    // Per spec (§7.2): == null / != null have explicit null semantics
    // They behave like is_null / not_null respectively
    if (node.cmp_op == "==") {
      // x == null → is_null(x), null == x → is_null(x), null == null → true
      if (a_is_null || b_is_null) {
        return a_is_null && b_is_null;
      }
    }
    if (node.cmp_op == "!=") {
      // x != null → not_null(x), null != x → not_null(x), null != null → false
      if (a_is_null || b_is_null) {
        return a_is_null != b_is_null;
      }
    }

    // Per spec: other comparisons with null yield false
    // This means not (x > 5) with null x returns true (row included)
    if (a_is_null || b_is_null) {
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
    // String list membership requires string columns (not yet implemented)
    if (!node.in_list_str.empty()) {
      throw std::runtime_error(
          "String membership (in list with strings) not yet supported - "
          "requires dictionary-encoded string columns");
    }

    ExprResult lhs = eval_expr(*node.value_a, row, batch, ctx);

    // If lhs is null, in yields false (per spec: null is not a member of any list)
    // This is different from cmp which yields unknown - in is a membership test
    // where null definitively is not in the set of literal values
    if (!lhs) {
      return false;
    }

    double val = *lhs;

    // Check if value is in the numeric list
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
