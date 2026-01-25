#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "column_batch.h"
#include "key_registry.h"
#include "param_table.h"
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

TEST_CASE("sort orders floats ascending with nulls last", "[sort][task]") {
  auto &registry = TaskRegistry::instance();
  auto ctx = make_test_ctx();

  // Build input batch with a float column
  auto base = std::make_shared<ColumnBatch>(5);
  for (size_t i = 0; i < 5; ++i) {
    base->setId(i, static_cast<int64_t>(i + 1));
  }

  auto scores = std::make_shared<FloatColumn>(5);
  scores->values = {0.4, 0.9, -1.0, 0.0, 0.4};
  scores->valid = {1, 1, 1, 0, 1}; // row 3 is null

  auto batch =
      std::make_shared<ColumnBatch>(base->withFloatColumn(key_id(KeyId::final_score), scores));
  RowSet input(batch);

  nlohmann::json params;
  params["by"] = key_id(KeyId::final_score);
  auto validated = registry.validate_params("core::sort", params);

  RowSet result = registry.execute("core::sort", {input}, validated, ctx);
  REQUIRE(result.rowCount() == 5);
  REQUIRE(result.logicalSize() == 5);

  // Expect order: idx2 (-1.0), idx0 (0.4), idx4 (0.4 tie, stable), idx1 (0.9), idx3 (null)
  auto ordered = result.activeRows().toVector(result.rowCount());
  REQUIRE(ordered == std::vector<RowIndex>{2, 0, 4, 1, 3});
}

TEST_CASE("sort respects selection/order for strings and desc ordering", "[sort][task]") {
  auto &registry = TaskRegistry::instance();
  auto ctx = make_test_ctx();

  auto base = std::make_shared<ColumnBatch>(4);
  for (size_t i = 0; i < 4; ++i) {
    base->setId(i, static_cast<int64_t>(10 + i));
  }

  auto dict = std::make_shared<std::vector<std::string>>(
      std::initializer_list<std::string>{"a", "b", "c"});
  auto codes = std::make_shared<std::vector<int32_t>>(
      std::initializer_list<int32_t>{0, 1, 0, 2});
  auto valid = std::make_shared<std::vector<uint8_t>>(
      std::initializer_list<uint8_t>{1, 1, 0, 1});
  auto str_col = std::make_shared<StringDictColumn>(dict, codes, valid);

  auto batch =
      std::make_shared<ColumnBatch>(base->withStringColumn(key_id(KeyId::country), str_col));
  RowSet input(batch);

  // Apply selection {0,1,2} and an initial order [2,0,1]
  SelectionVector sel{0, 1, 2};
  Permutation ord{2, 0, 1};
  input = input.withSelection(sel).withOrder(ord);

  nlohmann::json params;
  params["by"] = key_id(KeyId::country);
  params["order"] = "desc";
  auto validated = registry.validate_params("core::sort", params);

  RowSet result = registry.execute("core::sort", {input}, validated, ctx);
  REQUIRE(result.rowCount() == 4);
  REQUIRE(result.logicalSize() == 3); // selection preserved

  // Selected rows have values: idx2=null, idx0="a", idx1="b" -> desc: b, a, null
  auto ordered = result.activeRows().toVector(result.rowCount());
  REQUIRE(ordered == std::vector<RowIndex>{1, 0, 2});
}

TEST_CASE("sort handles string null-null comparisons safely", "[sort][task]") {
  auto &registry = TaskRegistry::instance();
  auto ctx = make_test_ctx();

  auto base = std::make_shared<ColumnBatch>(2);
  base->setId(0, 1);
  base->setId(1, 2);

  auto dict = std::make_shared<std::vector<std::string>>(
      std::initializer_list<std::string>{"x"});
  auto codes = std::make_shared<std::vector<int32_t>>(
      std::initializer_list<int32_t>{0, 0});
  auto valid = std::make_shared<std::vector<uint8_t>>(
      std::initializer_list<uint8_t>{0, 0}); // both null
  auto str_col = std::make_shared<StringDictColumn>(dict, codes, valid);

  auto batch =
      std::make_shared<ColumnBatch>(base->withStringColumn(key_id(KeyId::country), str_col));
  RowSet input(batch);

  nlohmann::json params;
  params["by"] = key_id(KeyId::country);
  auto validated = registry.validate_params("core::sort", params);

  RowSet result = registry.execute("core::sort", {input}, validated, ctx);
  REQUIRE(result.rowCount() == 2);
  REQUIRE(result.logicalSize() == 2);

  // Both nulls are equal; stable_sort keeps original order
  auto ordered = result.activeRows().toVector(result.rowCount());
  REQUIRE(ordered == std::vector<RowIndex>{0, 1});
}

TEST_CASE("sort rejects invalid params or unsupported keys", "[sort][task]") {
  auto &registry = TaskRegistry::instance();
  auto ctx = make_test_ctx();

  // Minimal batch to satisfy executor
  auto base = std::make_shared<ColumnBatch>(1);
  base->setId(0, 1);
  RowSet input(base);

  auto expect_throw = [&](const nlohmann::json &params,
                          const std::string &msg) {
    auto validated = registry.validate_params("core::sort", params);
    try {
      (void)registry.execute("core::sort", {input}, validated, ctx);
      FAIL("core::sort did not throw");
    } catch (const std::exception &e) {
      REQUIRE(std::string(e.what()) == msg);
    }
  };

  SECTION("bad order value") {
    nlohmann::json params;
    params["by"] = key_id(KeyId::id);
    params["order"] = "sideways";
    expect_throw(params, "sort: 'order' must be 'asc' or 'desc' if provided");
  }

  SECTION("unsupported key type (feature bundle)") {
    nlohmann::json params;
    params["by"] = key_id(KeyId::features_esr);
    expect_throw(params, "sort: key 'features_esr' is not sortable");
  }

  SECTION("missing column for float key") {
    nlohmann::json params;
    params["by"] = key_id(KeyId::final_score);
    expect_throw(params, "sort: column for key 'final_score' not found");
  }
}
