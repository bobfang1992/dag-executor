#pragma once

#include "column_batch.h"
#include "expr_eval.h"
#include "plan.h"
#include <optional>

namespace rankd {

// Three-valued predicate result: true, false, or unknown (nullopt)
// Null semantics (per spec §7.2):
// - "Only == null / != null (or is_null) have explicit null semantics"
//   - This means LITERAL const_null in AST, not runtime null values
//   - x == null (const_null literal) → true if x is null, false otherwise
//   - x != null (const_null literal) → true if x is not null, false otherwise
// - "Other comparisons with null evaluate to false"
//   - x == y (y is runtime null) → false
//   - x != 0 (x is runtime null) → false (NOT true!)
//   - x > 5 (x is null) → false
// - in with null lhs yields false (null is not a member of any literal list)
// - is_null/not_null always yield true/false (never unknown)
// - NOT, AND, OR use three-valued logic if operands are unknown
// Note: Since most null comparisons return false, NOT/AND/OR will see false,
// e.g., not (x > 5) with null x returns not false = true
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
    // Per spec (§7.2): "Only == null / != null (or is_null) have explicit null semantics.
    // Other comparisons with null evaluate to false."
    //
    // Key distinction: "== null" means a LITERAL const_null in the AST, not a runtime null.
    // - Key.x == null (const_null literal) → special semantics (like is_null)
    // - Key.x == Key.y (y is null at runtime) → returns false
    // - Key.x != 0 (x is null at runtime) → returns false (NOT true!)

    // Check if either operand is a literal const_null in the AST
    bool a_is_const_null = node.value_a && node.value_a->op == "const_null";
    bool b_is_const_null = node.value_b && node.value_b->op == "const_null";
    bool is_explicit_null_cmp = a_is_const_null || b_is_const_null;

    ExprResult a = eval_expr(*node.value_a, row, batch, ctx);
    ExprResult b = eval_expr(*node.value_b, row, batch, ctx);

    bool a_is_null = !a.has_value();
    bool b_is_null = !b.has_value();

    // Special semantics ONLY for explicit null comparisons (const_null in AST)
    if (is_explicit_null_cmp) {
      if (node.cmp_op == "==") {
        // x == null → is_null(x), null == null → true
        return a_is_null && b_is_null;
      }
      if (node.cmp_op == "!=") {
        // x != null → not_null(x), null != null → false
        return a_is_null != b_is_null;
      }
      // For <, <=, >, >= with explicit null literal, still return false
    }

    // All other cases: any comparison with runtime null → false
    // This includes: Key.x != 0 where x is null, Key.x == Key.y where y is null, etc.
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
