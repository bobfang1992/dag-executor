#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "column_batch.h"
#include "executor.h"
#include "param_table.h"
#include "plan.h"
#include "rowset.h"
#include "task_registry.h"

using namespace rankd;

// Empty context for tests
static ExecCtx make_test_ctx() {
  ExecCtx ctx;
  static ParamTable empty_params;
  ctx.params = &empty_params;
  return ctx;
}

TEST_CASE("concat task produces correct output", "[concat][task]") {
  auto &registry = TaskRegistry::instance();
  auto ctx = make_test_ctx();

  SECTION("concat two sources from viewer.follow and fetch_cached_recommendation") {
    // Create source from viewer.follow (ids 1-4)
    nlohmann::json follow_params;
    follow_params["fanout"] = 4;
    auto fp = registry.validate_params("viewer.follow", follow_params);
    RowSet lhs = registry.execute("viewer.follow", {}, fp, ctx);

    // Create source from viewer.fetch_cached_recommendation (ids 1001-1004)
    nlohmann::json fetch_params;
    fetch_params["fanout"] = 4;
    auto fcp = registry.validate_params("viewer.fetch_cached_recommendation", fetch_params);
    RowSet rhs = registry.execute("viewer.fetch_cached_recommendation", {}, fcp, ctx);

    // Concat them - now uses params.rhs instead of inputs[1]
    nlohmann::json concat_params;
    concat_params["rhs"] = "rhs_node";  // node_id reference (not used in direct test)
    auto cp = registry.validate_params("concat", concat_params);

    // Set up resolved_node_refs for the task execution
    std::unordered_map<std::string, RowSet> resolved_refs;
    resolved_refs.emplace("rhs", rhs);
    ExecCtx exec_ctx = ctx;
    exec_ctx.resolved_node_refs = &resolved_refs;

    RowSet result = registry.execute("concat", {lhs}, cp, exec_ctx);

    REQUIRE(result.rowCount() == 8);
    REQUIRE(result.logicalSize() == 8);

    // Check ids: [1,2,3,4,1001,1002,1003,1004]
    auto indices = result.materializeIndexViewForOutput(result.batch().size());
    REQUIRE(indices.size() == 8);

    std::vector<int64_t> ids;
    for (uint32_t idx : indices) {
      ids.push_back(result.batch().getId(idx));
    }
    REQUIRE(ids == std::vector<int64_t>{1, 2, 3, 4, 1001, 1002, 1003, 1004});

    // Check country string column exists and has unified dict
    // viewer.follow emits country = ["US", "CA"] cycling
    // viewer.fetch_cached_recommendation emits country = ["CA", "FR"] cycling
    // Unified dict should be: lhs dict in order + rhs entries not in lhs
    // lhs dict = ["US", "CA"], rhs dict = ["CA", "FR"]
    // Unified should be ["US", "CA", "FR"]
    const auto *country_col = result.batch().getStringCol(key_id(KeyId::country));
    REQUIRE(country_col != nullptr);
    REQUIRE(country_col->dict->size() == 3);
    REQUIRE((*country_col->dict)[0] == "US");
    REQUIRE((*country_col->dict)[1] == "CA");
    REQUIRE((*country_col->dict)[2] == "FR");

    // Check codes for country
    // lhs rows: [US, CA, US, CA] -> codes [0, 1, 0, 1]
    // rhs rows: [CA, FR, CA, FR] -> remap: CA->1, FR->2 -> codes [1, 2, 1, 2]
    std::vector<int32_t> expected_codes = {0, 1, 0, 1, 1, 2, 1, 2};
    for (size_t i = 0; i < 8; ++i) {
      REQUIRE((*country_col->valid)[i] == 1);
      REQUIRE((*country_col->codes)[i] == expected_codes[i]);
    }
  }

  SECTION("concat missing rhs param throws at validate_params") {
    nlohmann::json concat_params;
    // No rhs param - should fail
    REQUIRE_THROWS_WITH(
        registry.validate_params("concat", concat_params),
        "Invalid params for op 'concat': missing required field 'rhs'");
  }

  SECTION("concat with wrong input arity (0 inputs) throws") {
    nlohmann::json follow_params;
    follow_params["fanout"] = 4;
    auto fp = registry.validate_params("viewer.follow", follow_params);
    RowSet rhs = registry.execute("viewer.follow", {}, fp, ctx);

    nlohmann::json concat_params;
    concat_params["rhs"] = "rhs_node";
    auto cp = registry.validate_params("concat", concat_params);

    std::unordered_map<std::string, RowSet> resolved_refs;
    resolved_refs.emplace("rhs", rhs);
    ExecCtx exec_ctx = ctx;
    exec_ctx.resolved_node_refs = &resolved_refs;

    REQUIRE_THROWS_WITH(
        registry.execute("concat", {}, cp, exec_ctx),
        "Error: op 'concat' expects exactly 1 input, got 0");
  }

  SECTION("concat with wrong input arity (2 inputs) throws") {
    nlohmann::json follow_params;
    follow_params["fanout"] = 2;
    auto fp = registry.validate_params("viewer.follow", follow_params);
    RowSet a = registry.execute("viewer.follow", {}, fp, ctx);
    RowSet b = registry.execute("viewer.follow", {}, fp, ctx);
    RowSet c = registry.execute("viewer.follow", {}, fp, ctx);

    nlohmann::json concat_params;
    concat_params["rhs"] = "rhs_node";
    auto cp = registry.validate_params("concat", concat_params);

    std::unordered_map<std::string, RowSet> resolved_refs;
    resolved_refs.emplace("rhs", c);
    ExecCtx exec_ctx = ctx;
    exec_ctx.resolved_node_refs = &resolved_refs;

    REQUIRE_THROWS_WITH(
        registry.execute("concat", {a, b}, cp, exec_ctx),
        "Error: op 'concat' expects exactly 1 input, got 2");
  }

  SECTION("concat with missing resolved_node_refs throws") {
    nlohmann::json follow_params;
    follow_params["fanout"] = 4;
    auto fp = registry.validate_params("viewer.follow", follow_params);
    RowSet lhs = registry.execute("viewer.follow", {}, fp, ctx);

    nlohmann::json concat_params;
    concat_params["rhs"] = "rhs_node";
    auto cp = registry.validate_params("concat", concat_params);

    // No resolved_node_refs set - should fail
    REQUIRE_THROWS_WITH(
        registry.execute("concat", {lhs}, cp, ctx),
        "Error: op 'concat' missing resolved 'rhs' NodeRef");
  }
}

TEST_CASE("concat_demo.plan.json executes correctly", "[concat][plan]") {
  Plan plan = parse_plan("artifacts/plans/concat_demo.plan.json");
  validate_plan(plan);

  ExecCtx ctx;
  ParamTable params;
  ctx.params = &params;
  ctx.expr_table = &plan.expr_table;
  ctx.pred_table = &plan.pred_table;

  auto result = execute_plan(plan, ctx);

  REQUIRE(result.outputs.size() == 1);
  REQUIRE(result.outputs[0].rowCount() == 8);
  REQUIRE(result.outputs[0].logicalSize() == 8);

  // Check output ids
  auto indices = result.outputs[0].materializeIndexViewForOutput(result.outputs[0].batch().size());
  std::vector<int64_t> ids;
  for (uint32_t idx : indices) {
    ids.push_back(result.outputs[0].batch().getId(idx));
  }
  REQUIRE(ids == std::vector<int64_t>{1, 2, 3, 4, 1001, 1002, 1003, 1004});
}

TEST_CASE("concat_bad_arity.plan.json fails validation (missing rhs)", "[concat][plan]") {
  Plan plan = parse_plan("artifacts/plans/concat_bad_arity.plan.json");

  // Validation should fail because rhs param is missing
  REQUIRE_THROWS_WITH(
      validate_plan(plan),
      "Node 'n1': Invalid params for op 'concat': missing required field 'rhs'");
}
