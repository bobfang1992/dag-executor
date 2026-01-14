#pragma once

#include "column_batch.h"
#include "expr_eval.h"
#include "plan.h"

namespace rankd {

// Predicate evaluation result: true, false, or evaluation error (throws)
// Note: predicates never return null - null comparisons yield false
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

    // If either side is null, comparison yields false
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
