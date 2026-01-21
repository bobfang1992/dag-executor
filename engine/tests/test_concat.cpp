#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "column_batch.h"
#include "endpoint_registry.h"
#include "executor.h"
#include "io_clients.h"
#include "param_table.h"
#include "plan.h"
#include "request.h"
#include "rowset.h"
#include "task_registry.h"
#include <optional>

using namespace rankd;

// Helper to load endpoint registry for tests
static const EndpointRegistry& get_test_endpoint_registry() {
  static std::optional<EndpointRegistry> registry;
  if (!registry) {
    auto result = EndpointRegistry::LoadFromJson("artifacts/endpoints.dev.json");
    if (std::holds_alternative<EndpointRegistry>(result)) {
      registry = std::get<EndpointRegistry>(result);
    } else {
      throw std::runtime_error("Failed to load endpoint registry: " + std::get<std::string>(result));
    }
  }
  return *registry;
}

// Empty context for tests
static ExecCtx make_test_ctx() {
  ExecCtx ctx;
  static ParamTable empty_params;
  ctx.params = &empty_params;
  return ctx;
}

// Helper to create test RowSet with ids and country strings
static RowSet create_test_rowset(const std::vector<int64_t>& ids,
                                   const std::vector<std::string>& countries) {
  // Build ColumnBatch with ids
  auto batch_ptr = std::make_shared<ColumnBatch>(ids.size());
  for (size_t i = 0; i < ids.size(); ++i) {
    batch_ptr->setId(i, ids[i]);
  }

  // Build string dict column for country
  // First, build the dictionary (unique values in order)
  std::vector<std::string> dict;
  std::unordered_map<std::string, int32_t> dict_map;
  std::vector<int32_t> codes(countries.size());
  std::vector<uint8_t> valid(countries.size(), 1);

  for (size_t i = 0; i < countries.size(); ++i) {
    const auto& country = countries[i];
    auto it = dict_map.find(country);
    if (it == dict_map.end()) {
      int32_t code = static_cast<int32_t>(dict.size());
      dict.push_back(country);
      dict_map[country] = code;
      codes[i] = code;
    } else {
      codes[i] = it->second;
    }
  }

  auto string_col = std::make_shared<StringDictColumn>(
      std::make_shared<std::vector<std::string>>(std::move(dict)),
      std::make_shared<std::vector<int32_t>>(std::move(codes)),
      std::make_shared<std::vector<uint8_t>>(std::move(valid)));

  // Use withStringColumn to create a new batch with the string column
  auto batch_with_country = std::make_shared<ColumnBatch>(
      batch_ptr->withStringColumn(key_id(KeyId::country), string_col));

  return RowSet(batch_with_country);
}

TEST_CASE("concat task produces correct output", "[concat][task]") {
  auto &registry = TaskRegistry::instance();
  auto ctx = make_test_ctx();

  SECTION("concat two sources with string columns") {
    // Create source (ids 1-4, countries cycling US/CA)
    RowSet lhs = create_test_rowset({1, 2, 3, 4}, {"US", "CA", "US", "CA"});

    // Create source (ids 1001-1004, countries cycling CA/FR)
    RowSet rhs = create_test_rowset({1001, 1002, 1003, 1004}, {"CA", "FR", "CA", "FR"});

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
    RowSet rhs = create_test_rowset({1, 2, 3, 4}, {"US", "CA", "US", "CA"});

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
    RowSet a = create_test_rowset({1, 2}, {"US", "CA"});
    RowSet b = create_test_rowset({3, 4}, {"US", "CA"});
    RowSet c = create_test_rowset({5, 6}, {"US", "CA"});

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
    RowSet lhs = create_test_rowset({1, 2, 3, 4}, {"US", "CA", "US", "CA"});

    nlohmann::json concat_params;
    concat_params["rhs"] = "rhs_node";
    auto cp = registry.validate_params("concat", concat_params);

    // No resolved_node_refs set - should fail
    REQUIRE_THROWS_WITH(
        registry.execute("concat", {lhs}, cp, ctx),
        "Error: op 'concat' missing resolved 'rhs' NodeRef");
  }
}

TEST_CASE("concat_plan.plan.json executes correctly", "[concat][plan][integration]") {
  Plan plan = parse_plan("artifacts/plans/concat_plan.plan.json");
  validate_plan(plan, &get_test_endpoint_registry());

  IoClients io_clients;
  ExecCtx ctx;
  ParamTable params;
  RequestContext request_ctx;
  request_ctx.user_id = 1;
  request_ctx.request_id = "test";
  ctx.params = &params;
  ctx.expr_table = &plan.expr_table;
  ctx.pred_table = &plan.pred_table;
  ctx.request = &request_ctx;
  ctx.endpoints = &get_test_endpoint_registry();
  ctx.clients = &io_clients;

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
      validate_plan(plan, &get_test_endpoint_registry()),
      "Node 'n2': Invalid params for op 'concat': missing required field 'rhs'");
}
