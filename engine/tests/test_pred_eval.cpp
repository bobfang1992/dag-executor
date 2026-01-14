#include <catch2/catch_test_macros.hpp>

#include "column_batch.h"
#include "param_table.h"
#include "plan.h"
#include "pred_eval.h"

using namespace rankd;

// Helper to create a simple batch with one row
static std::shared_ptr<ColumnBatch> make_batch_with_id(int64_t id) {
  auto batch = std::make_shared<ColumnBatch>(1);
  batch->setId(0, id);
  return batch;
}

// Helper to create a batch with a float column
static std::shared_ptr<ColumnBatch>
make_batch_with_float(int64_t id, uint32_t key_id, double value, bool valid) {
  auto batch = std::make_shared<ColumnBatch>(1);
  batch->setId(0, id);
  auto col = std::make_shared<FloatColumn>(1);
  if (valid) {
    col->values[0] = value;
    col->valid[0] = 1;
  }
  return std::make_shared<ColumnBatch>(batch->withFloatColumn(key_id, col));
}

// Empty context for tests
static ExecCtx make_empty_ctx() {
  ExecCtx ctx;
  ctx.params = nullptr;
  ctx.expr_table = nullptr;
  ctx.pred_table = nullptr;
  return ctx;
}

TEST_CASE("const_bool predicate", "[pred_eval]") {
  auto batch = make_batch_with_id(1);
  auto ctx = make_empty_ctx();

  SECTION("const_bool true") {
    PredNode node;
    node.op = "const_bool";
    node.const_value = true;
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("const_bool false") {
    PredNode node;
    node.op = "const_bool";
    node.const_value = false;
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }
}

TEST_CASE("logical predicates (and, or, not)", "[pred_eval]") {
  auto batch = make_batch_with_id(1);
  auto ctx = make_empty_ctx();

  auto make_const = [](bool val) {
    auto node = std::make_shared<PredNode>();
    node->op = "const_bool";
    node->const_value = val;
    return node;
  };

  SECTION("and: true && true = true") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_const(true);
    node.pred_b = make_const(true);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("and: true && false = false") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_const(true);
    node.pred_b = make_const(false);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("and: false && true = false (short-circuit)") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_const(false);
    node.pred_b = make_const(true);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("or: false || true = true") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_const(false);
    node.pred_b = make_const(true);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("or: false || false = false") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_const(false);
    node.pred_b = make_const(false);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("or: true || false = true (short-circuit)") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_const(true);
    node.pred_b = make_const(false);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("not: !true = false") {
    PredNode node;
    node.op = "not";
    node.pred_a = make_const(true);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("not: !false = true") {
    PredNode node;
    node.op = "not";
    node.pred_a = make_const(false);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }
}

TEST_CASE("is_null and not_null predicates", "[pred_eval]") {
  auto ctx = make_empty_ctx();

  auto make_const_expr = [](double val) {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_number";
    node->const_value = val;
    return node;
  };

  auto make_null_expr = []() {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_null";
    return node;
  };

  SECTION("is_null with null value") {
    auto batch = make_batch_with_id(1);
    PredNode node;
    node.op = "is_null";
    node.value_a = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("is_null with non-null value") {
    auto batch = make_batch_with_id(1);
    PredNode node;
    node.op = "is_null";
    node.value_a = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("not_null with null value") {
    auto batch = make_batch_with_id(1);
    PredNode node;
    node.op = "not_null";
    node.value_a = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("not_null with non-null value") {
    auto batch = make_batch_with_id(1);
    PredNode node;
    node.op = "not_null";
    node.value_a = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("is_null with missing float column") {
    // key_id 9999 doesn't exist in batch
    auto batch = make_batch_with_id(1);
    PredNode node;
    node.op = "is_null";
    auto key_ref = std::make_shared<ExprNode>();
    key_ref->op = "key_ref";
    key_ref->key_id = 9999;
    node.value_a = key_ref;
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("not_null with valid float column") {
    auto batch = make_batch_with_float(1, 2001, 3.14, true);
    PredNode node;
    node.op = "not_null";
    auto key_ref = std::make_shared<ExprNode>();
    key_ref->op = "key_ref";
    key_ref->key_id = 2001;
    node.value_a = key_ref;
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("is_null with invalid float column") {
    auto batch = make_batch_with_float(1, 2001, 0.0, false);
    PredNode node;
    node.op = "is_null";
    auto key_ref = std::make_shared<ExprNode>();
    key_ref->op = "key_ref";
    key_ref->key_id = 2001;
    node.value_a = key_ref;
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }
}

TEST_CASE("cmp predicates with non-null values", "[pred_eval]") {
  auto batch = make_batch_with_id(1);
  auto ctx = make_empty_ctx();

  auto make_const_expr = [](double val) {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_number";
    node->const_value = val;
    return node;
  };

  SECTION("== with equal values") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "==";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("== with unequal values") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "==";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(3.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("!= with unequal values") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "!=";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(3.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("!= with equal values") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "!=";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("< comparison") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "<";
    node.value_a = make_const_expr(3.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);

    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(3.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("<= comparison") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "<=";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);

    node.value_a = make_const_expr(3.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);

    node.value_a = make_const_expr(6.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("> comparison") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = ">";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(3.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);

    node.value_a = make_const_expr(3.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION(">= comparison") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = ">=";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);

    node.value_a = make_const_expr(6.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);

    node.value_a = make_const_expr(3.0);
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }
}

TEST_CASE("cmp predicates with null operands (SQL semantics)", "[pred_eval]") {
  auto batch = make_batch_with_id(1);
  auto ctx = make_empty_ctx();

  auto make_const_expr = [](double val) {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_number";
    node->const_value = val;
    return node;
  };

  auto make_null_expr = []() {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_null";
    return node;
  };

  SECTION("== with left null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "==";
    node.value_a = make_null_expr();
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("== with right null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "==";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("== with both null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "==";
    node.value_a = make_null_expr();
    node.value_b = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("!= with left null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "!=";
    node.value_a = make_null_expr();
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("!= with right null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "!=";
    node.value_a = make_const_expr(5.0);
    node.value_b = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("!= with both null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "!=";
    node.value_a = make_null_expr();
    node.value_b = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("< with null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "<";
    node.value_a = make_null_expr();
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);

    node.value_a = make_const_expr(5.0);
    node.value_b = make_null_expr();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("<= with null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = "<=";
    node.value_a = make_null_expr();
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("> with null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = ">";
    node.value_a = make_null_expr();
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION(">= with null returns false") {
    PredNode node;
    node.op = "cmp";
    node.cmp_op = ">=";
    node.value_a = make_null_expr();
    node.value_b = make_const_expr(5.0);
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }
}

TEST_CASE("in predicate", "[pred_eval]") {
  auto batch = make_batch_with_id(1);
  auto ctx = make_empty_ctx();

  auto make_const_expr = [](double val) {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_number";
    node->const_value = val;
    return node;
  };

  auto make_null_expr = []() {
    auto node = std::make_shared<ExprNode>();
    node->op = "const_null";
    return node;
  };

  SECTION("value in list") {
    PredNode node;
    node.op = "in";
    node.value_a = make_const_expr(3.0);
    node.in_list = {1.0, 2.0, 3.0, 4.0, 5.0};
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("value not in list") {
    PredNode node;
    node.op = "in";
    node.value_a = make_const_expr(10.0);
    node.in_list = {1.0, 2.0, 3.0, 4.0, 5.0};
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("null value in list returns false") {
    PredNode node;
    node.op = "in";
    node.value_a = make_null_expr();
    node.in_list = {1.0, 2.0, 3.0};
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("empty list") {
    PredNode node;
    node.op = "in";
    node.value_a = make_const_expr(5.0);
    node.in_list = {};
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }
}

TEST_CASE("key_ref in predicates", "[pred_eval]") {
  auto ctx = make_empty_ctx();

  SECTION("key_ref to Key.id (key_id=1)") {
    auto batch = make_batch_with_id(42);

    PredNode node;
    node.op = "cmp";
    node.cmp_op = "==";

    auto key_ref = std::make_shared<ExprNode>();
    key_ref->op = "key_ref";
    key_ref->key_id = 1; // Key.id

    auto const_val = std::make_shared<ExprNode>();
    const_val->op = "const_number";
    const_val->const_value = 42.0;

    node.value_a = key_ref;
    node.value_b = const_val;

    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("key_ref to float column") {
    auto batch = make_batch_with_float(1, 2001, 0.75, true);

    PredNode node;
    node.op = "cmp";
    node.cmp_op = ">=";

    auto key_ref = std::make_shared<ExprNode>();
    key_ref->op = "key_ref";
    key_ref->key_id = 2001;

    auto const_val = std::make_shared<ExprNode>();
    const_val->op = "const_number";
    const_val->const_value = 0.6;

    node.value_a = key_ref;
    node.value_b = const_val;

    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }
}

TEST_CASE("three-valued logic with null (SQL semantics)", "[pred_eval]") {
  auto batch = make_batch_with_id(1);
  auto ctx = make_empty_ctx();

  // Helper to create a comparison predicate that yields unknown (null operand)
  auto make_null_cmp = []() {
    auto pred = std::make_shared<PredNode>();
    pred->op = "cmp";
    pred->cmp_op = ">=";
    auto null_expr = std::make_shared<ExprNode>();
    null_expr->op = "const_null";
    auto const_expr = std::make_shared<ExprNode>();
    const_expr->op = "const_number";
    const_expr->const_value = 5.0;
    pred->value_a = null_expr;
    pred->value_b = const_expr;
    return pred;
  };

  auto make_true_pred = []() {
    auto pred = std::make_shared<PredNode>();
    pred->op = "const_bool";
    pred->const_value = true;
    return pred;
  };

  auto make_false_pred = []() {
    auto pred = std::make_shared<PredNode>();
    pred->op = "const_bool";
    pred->const_value = false;
    return pred;
  };

  SECTION("NOT unknown = false (in filter context)") {
    // not (null >= 5) should return false, not true
    PredNode node;
    node.op = "not";
    node.pred_a = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("true AND unknown = false (in filter context)") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_true_pred();
    node.pred_b = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("unknown AND true = false (in filter context)") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_null_cmp();
    node.pred_b = make_true_pred();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("false AND unknown = false") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_false_pred();
    node.pred_b = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("unknown AND false = false") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_null_cmp();
    node.pred_b = make_false_pred();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("unknown AND unknown = false (in filter context)") {
    PredNode node;
    node.op = "and";
    node.pred_a = make_null_cmp();
    node.pred_b = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("true OR unknown = true") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_true_pred();
    node.pred_b = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("unknown OR true = true") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_null_cmp();
    node.pred_b = make_true_pred();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == true);
  }

  SECTION("false OR unknown = false (in filter context)") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_false_pred();
    node.pred_b = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("unknown OR false = false (in filter context)") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_null_cmp();
    node.pred_b = make_false_pred();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("unknown OR unknown = false (in filter context)") {
    PredNode node;
    node.op = "or";
    node.pred_a = make_null_cmp();
    node.pred_b = make_null_cmp();
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }

  SECTION("NOT NOT unknown = false (in filter context)") {
    // Double negation of unknown is still unknown -> false
    auto not_inner = std::make_shared<PredNode>();
    not_inner->op = "not";
    not_inner->pred_a = make_null_cmp();

    PredNode node;
    node.op = "not";
    node.pred_a = not_inner;
    REQUIRE(eval_pred(node, 0, *batch, ctx) == false);
  }
}
