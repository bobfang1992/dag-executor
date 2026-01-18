#include <catch2/catch_test_macros.hpp>

#include "executor.h"
#include "key_registry.h"
#include "param_table.h"
#include "plan.h"
#include "schema_delta.h"
#include <algorithm>
#include <optional>

using namespace rankd;

// Helper to check keys are sorted and unique
static bool is_sorted_unique(const std::vector<uint32_t> &keys) {
  if (keys.empty())
    return true;
  for (size_t i = 1; i < keys.size(); ++i) {
    if (keys[i] <= keys[i - 1])
      return false;
  }
  return true;
}

// Helper to find a node's schema delta by op (returns nullopt if not found)
static std::optional<NodeSchemaDelta>
find_delta_by_op(const std::vector<NodeSchemaDelta> &deltas,
                 const Plan &plan, const std::string &op) {
  for (const auto &nd : deltas) {
    // Find the node in the plan to get its op
    for (const auto &node : plan.nodes) {
      if (node.node_id == nd.node_id && node.op == op) {
        return nd;
      }
    }
  }
  return std::nullopt;
}

// Helper to find all deltas for a given op (for plans with multiple nodes of same op)
static std::vector<NodeSchemaDelta>
find_all_deltas_by_op(const std::vector<NodeSchemaDelta> &deltas,
                      const Plan &plan, const std::string &op) {
  std::vector<NodeSchemaDelta> result;
  for (const auto &nd : deltas) {
    for (const auto &node : plan.nodes) {
      if (node.node_id == nd.node_id && node.op == op) {
        result.push_back(nd);
        break;
      }
    }
  }
  return result;
}

// Empty context for tests
static ExecCtx make_test_ctx(const Plan &plan) {
  ExecCtx ctx;
  static ParamTable empty_params;
  ctx.params = &empty_params;
  ctx.expr_table = &plan.expr_table;
  ctx.pred_table = &plan.pred_table;
  return ctx;
}

TEST_CASE("Runtime schema delta: vm_and_row_ops fixture",
          "[schema_delta][runtime]") {
  Plan plan =
      parse_plan("engine/tests/fixtures/plan_info/vm_and_row_ops.plan.json");
  validate_plan(plan);
  auto ctx = make_test_ctx(plan);

  auto result = execute_plan(plan, ctx);

  // Should have 4 nodes worth of schema deltas
  REQUIRE(result.schema_deltas.size() == 4);

  SECTION("Source node (viewer.follow) has non-empty new_keys") {
    auto delta_opt = find_delta_by_op(result.schema_deltas, plan, "viewer.follow");
    REQUIRE(delta_opt.has_value());
    auto delta = delta_opt.value();

    // Source node should add columns
    REQUIRE(delta.delta.new_keys.size() > 0);
    // Source node should not remove any columns (no inputs)
    REQUIRE(delta.delta.removed_keys.empty());
    // Input keys union should be empty (source has no inputs)
    REQUIRE(delta.delta.in_keys_union.empty());
    // Keys should be sorted and unique
    REQUIRE(is_sorted_unique(delta.delta.new_keys));
    REQUIRE(is_sorted_unique(delta.delta.out_keys));
  }

  SECTION("VM node adds out_key to new_keys") {
    auto delta_opt = find_delta_by_op(result.schema_deltas, plan, "vm");
    REQUIRE(delta_opt.has_value());
    auto delta = delta_opt.value();

    // VM node should add the out_key (2001 = final_score)
    // Using lenient assertion: new_keys contains out_key
    uint32_t out_key = 2001; // final_score from fixture
    bool has_out_key = std::find(delta.delta.new_keys.begin(),
                                  delta.delta.new_keys.end(),
                                  out_key) != delta.delta.new_keys.end();
    REQUIRE(has_out_key);
    // VM should not remove any columns
    REQUIRE(delta.delta.removed_keys.empty());
    // Keys should be sorted and unique
    REQUIRE(is_sorted_unique(delta.delta.new_keys));
    REQUIRE(is_sorted_unique(delta.delta.removed_keys));
    REQUIRE(is_sorted_unique(delta.delta.out_keys));
  }

  SECTION("Filter node (row-only) has empty new_keys and removed_keys") {
    auto delta_opt = find_delta_by_op(result.schema_deltas, plan, "filter");
    REQUIRE(delta_opt.has_value());
    auto delta = delta_opt.value();

    // Row-only ops should not add or remove columns
    REQUIRE(delta.delta.new_keys.empty());
    REQUIRE(delta.delta.removed_keys.empty());
    // Keys should be sorted and unique
    REQUIRE(is_sorted_unique(delta.delta.in_keys_union));
    REQUIRE(is_sorted_unique(delta.delta.out_keys));
  }

  SECTION("Take node (row-only) has empty new_keys and removed_keys") {
    auto delta_opt = find_delta_by_op(result.schema_deltas, plan, "take");
    REQUIRE(delta_opt.has_value());
    auto delta = delta_opt.value();

    // Row-only ops should not add or remove columns
    REQUIRE(delta.delta.new_keys.empty());
    REQUIRE(delta.delta.removed_keys.empty());
    // Keys should be sorted and unique
    REQUIRE(is_sorted_unique(delta.delta.in_keys_union));
    REQUIRE(is_sorted_unique(delta.delta.out_keys));
  }
}

TEST_CASE("Runtime schema delta: fixed_source fixture (concat)",
          "[schema_delta][runtime]") {
  Plan plan =
      parse_plan("engine/tests/fixtures/plan_info/fixed_source.plan.json");
  validate_plan(plan);
  auto ctx = make_test_ctx(plan);

  auto result = execute_plan(plan, ctx);

  // Should have 4 nodes worth of schema deltas
  REQUIRE(result.schema_deltas.size() == 4);

  SECTION("Source nodes have non-empty new_keys") {
    // viewer.follow source
    auto follow_delta =
        find_delta_by_op(result.schema_deltas, plan, "viewer.follow");
    REQUIRE(follow_delta.has_value());
    REQUIRE(follow_delta->delta.new_keys.size() > 0);
    REQUIRE(follow_delta->delta.removed_keys.empty());
    REQUIRE(follow_delta->delta.in_keys_union.empty());
    REQUIRE(is_sorted_unique(follow_delta->delta.new_keys));

    // viewer.fetch_cached_recommendation source
    auto cached_delta = find_delta_by_op(result.schema_deltas, plan,
                                          "viewer.fetch_cached_recommendation");
    REQUIRE(cached_delta.has_value());
    REQUIRE(cached_delta->delta.new_keys.size() > 0);
    REQUIRE(cached_delta->delta.removed_keys.empty());
    REQUIRE(cached_delta->delta.in_keys_union.empty());
    REQUIRE(is_sorted_unique(cached_delta->delta.new_keys));
  }

  SECTION("Concat node (binary, row-only) has empty new_keys") {
    auto delta_opt = find_delta_by_op(result.schema_deltas, plan, "concat");
    REQUIRE(delta_opt.has_value());
    auto delta = delta_opt.value();

    // Concat merges schemas but doesn't add new columns
    // (all columns from inputs are preserved)
    REQUIRE(delta.delta.new_keys.empty());
    REQUIRE(delta.delta.removed_keys.empty());
    // in_keys_union should be non-empty (union of both inputs)
    REQUIRE(delta.delta.in_keys_union.size() > 0);
    // Keys should be sorted and unique
    REQUIRE(is_sorted_unique(delta.delta.in_keys_union));
    REQUIRE(is_sorted_unique(delta.delta.out_keys));
  }

  SECTION("Take node (row-only) has empty new_keys and removed_keys") {
    auto delta_opt = find_delta_by_op(result.schema_deltas, plan, "take");
    REQUIRE(delta_opt.has_value());
    auto delta = delta_opt.value();

    REQUIRE(delta.delta.new_keys.empty());
    REQUIRE(delta.delta.removed_keys.empty());
    REQUIRE(is_sorted_unique(delta.delta.in_keys_union));
    REQUIRE(is_sorted_unique(delta.delta.out_keys));
  }
}

TEST_CASE("Schema delta keys are always sorted and unique",
          "[schema_delta][runtime]") {
  SECTION("vm_and_row_ops fixture") {
    Plan plan =
        parse_plan("engine/tests/fixtures/plan_info/vm_and_row_ops.plan.json");
    validate_plan(plan);
    auto ctx = make_test_ctx(plan);
    auto result = execute_plan(plan, ctx);

    for (const auto &nd : result.schema_deltas) {
      INFO("Checking node: " << nd.node_id);
      REQUIRE(is_sorted_unique(nd.delta.in_keys_union));
      REQUIRE(is_sorted_unique(nd.delta.out_keys));
      REQUIRE(is_sorted_unique(nd.delta.new_keys));
      REQUIRE(is_sorted_unique(nd.delta.removed_keys));
    }
  }

  SECTION("fixed_source fixture") {
    Plan plan =
        parse_plan("engine/tests/fixtures/plan_info/fixed_source.plan.json");
    validate_plan(plan);
    auto ctx = make_test_ctx(plan);
    auto result = execute_plan(plan, ctx);

    for (const auto &nd : result.schema_deltas) {
      INFO("Checking node: " << nd.node_id);
      REQUIRE(is_sorted_unique(nd.delta.in_keys_union));
      REQUIRE(is_sorted_unique(nd.delta.out_keys));
      REQUIRE(is_sorted_unique(nd.delta.new_keys));
      REQUIRE(is_sorted_unique(nd.delta.removed_keys));
    }
  }
}
