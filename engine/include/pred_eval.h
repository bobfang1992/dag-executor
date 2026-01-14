#pragma once

#include "column_batch.h"
#include "expr_eval.h"
#include "plan.h"

namespace rankd {

// Predicate evaluation result: true, false, or evaluation error (throws)
// Null semantics:
// - == and != handle null meaningfully (null == null is true, x != null is true if x is non-null)
// - Ordering comparisons (<, <=, >, >=) yield false if either operand is null
// - is_null/not_null explicitly test for null
// - in yields false if lhs is null
inline bool eval_pred(const PredNode &node, size_t row, const ColumnBatch &batch,
                      const ExecCtx &ctx) {
  if (node.op == "const_bool") {
    return node.const_value;
  }

  if (node.op == "and") {
    // Short-circuit: if a is false, don't evaluate b
    if (!eval_pred(*node.pred_a, row, batch, ctx)) {
      return false;
    }
    return eval_pred(*node.pred_b, row, batch, ctx);
  }

  if (node.op == "or") {
    // Short-circuit: if a is true, don't evaluate b
    if (eval_pred(*node.pred_a, row, batch, ctx)) {
      return true;
    }
    return eval_pred(*node.pred_b, row, batch, ctx);
  }

  if (node.op == "not") {
    return !eval_pred(*node.pred_a, row, batch, ctx);
  }

  if (node.op == "is_null") {
    ExprResult val = eval_expr(*node.value_a, row, batch, ctx);
    return !val.has_value();
  }

  if (node.op == "not_null") {
    ExprResult val = eval_expr(*node.value_a, row, batch, ctx);
    return val.has_value();
  }

  if (node.op == "cmp") {
    ExprResult a = eval_expr(*node.value_a, row, batch, ctx);
    ExprResult b = eval_expr(*node.value_b, row, batch, ctx);

    // Special handling for == and != with null operands
    // x == null returns true iff x is null (like is_null)
    // x != null returns true iff x is not null (like not_null)
    if (node.cmp_op == "==") {
      if (!a && !b) {
        return true; // null == null is true
      }
      if (!a || !b) {
        return false; // null == value or value == null is false
      }
      return *a == *b;
    }
    if (node.cmp_op == "!=") {
      if (!a && !b) {
        return false; // null != null is false
      }
      if (!a || !b) {
        return true; // null != value or value != null is true
      }
      return *a != *b;
    }

    // For ordering comparisons (<, <=, >, >=), null yields false
    if (!a || !b) {
      return false;
    }

    double av = *a;
    double bv = *b;

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

    // If lhs is null, in yields false
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

} // namespace rankd
