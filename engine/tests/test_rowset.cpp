#include <catch2/catch_test_macros.hpp>

#include "column_batch.h"
#include "param_table.h"
#include "rowset.h"
#include "task_registry.h"

using namespace rankd;

// Empty context for tests
static ExecCtx empty_ctx;

TEST_CASE("Batch with sequential ids", "[rowset]") {
  // Create batch directly (Redis-backed follow task requires Redis)
  auto batch = std::make_shared<ColumnBatch>(10);
  for (size_t i = 0; i < 10; ++i) {
    batch->setId(i, static_cast<int64_t>(i + 1));
  }
  RowSet source(batch);

  REQUIRE(source.batch().size() == 10);
  REQUIRE_FALSE(source.hasSelection());
  REQUIRE_FALSE(source.hasOrder());

  SECTION("ids are 1-indexed") {
    for (size_t i = 0; i < 10; ++i) {
      REQUIRE(source.batch().getId(i) == static_cast<int64_t>(i + 1));
    }
  }
}

TEST_CASE("take limits output and shares batch pointer", "[rowset][task]") {
  auto &registry = TaskRegistry::instance();

  // Create source batch manually (Redis-backed follow task requires Redis)
  auto batch = std::make_shared<ColumnBatch>(10);
  for (size_t i = 0; i < 10; ++i) {
    batch->setId(i, static_cast<int64_t>(i + 1));
  }
  RowSet source(batch);

  // take with count=5
  nlohmann::json take_params_json;
  take_params_json["count"] = 5;
  auto take_params = registry.validate_params("take", take_params_json);
  RowSet result = registry.execute("take", {source}, take_params, empty_ctx);

  SECTION("shares batch pointer (no copy)") {
    REQUIRE(result.batchPtr().get() == source.batchPtr().get());
  }

  SECTION("creates selection of first 5 elements") {
    REQUIRE(result.hasSelection());
    REQUIRE(result.logicalSize() == 5);
  }

  SECTION("output ids are [1,2,3,4,5]") {
    auto indices = result.materializeIndexViewForOutput(result.batch().size());
    REQUIRE(indices.size() == 5);

    std::vector<int64_t> output_ids;
    for (uint32_t idx : indices) {
      output_ids.push_back(result.batch().getId(idx));
    }
    REQUIRE(output_ids == std::vector<int64_t>{1, 2, 3, 4, 5});
  }

  SECTION("materialize_count stays at 0") {
    REQUIRE(result.batch().debug()->materialize_count == 0);
  }
}

TEST_CASE("RowSet iteration with selection and order", "[rowset]") {
  // Create batch with size=6, ids = [10, 20, 30, 40, 50, 60]
  auto debug = std::make_shared<DebugCounters>();
  auto batch = std::make_shared<ColumnBatch>(6, debug);
  for (size_t i = 0; i < 6; ++i) {
    batch->setId(i, static_cast<int64_t>((i + 1) * 10));
  }

  SECTION("order + selection filters correctly") {
    // selection = [0, 2, 3, 5] (indices 1 and 4 are filtered out)
    // order = [5, 4, 3, 2, 1, 0] (reverse order)
    // Expected iteration: order filtered by selection -> [5, 3, 2, 0]
    RowSet rs = RowSet(batch)
                    .withSelection(SelectionVector{0, 2, 3, 5})
                    .withOrder(Permutation{5, 4, 3, 2, 1, 0});

    auto indices = rs.materializeIndexViewForOutput(batch->size());

    REQUIRE(indices == std::vector<RowIndex>{5, 3, 2, 0});

    std::vector<int64_t> output_ids;
    for (RowIndex idx : indices) {
      output_ids.push_back(batch->getId(idx));
    }
    REQUIRE(output_ids == std::vector<int64_t>{60, 40, 30, 10});
    REQUIRE(debug->materialize_count == 0);
  }

  SECTION("order only") {
    RowSet rs = RowSet(batch).withOrder(Permutation{5, 3, 1, 4, 2, 0});

    auto indices = rs.materializeIndexViewForOutput(batch->size());
    REQUIRE(indices == std::vector<RowIndex>{5, 3, 1, 4, 2, 0});
  }

  SECTION("selection only") {
    RowSet rs = RowSet(batch).withSelection(SelectionVector{1, 3, 5});

    auto indices = rs.materializeIndexViewForOutput(batch->size());
    REQUIRE(indices == std::vector<RowIndex>{1, 3, 5});
  }

  SECTION("no selection, no order defaults to [0..N)") {
    RowSet rs(batch);

    auto indices = rs.materializeIndexViewForOutput(batch->size());
    REQUIRE(indices == std::vector<RowIndex>{0, 1, 2, 3, 4, 5});
  }
}

TEST_CASE("take with selection and order combined", "[rowset][task]") {
  auto &registry = TaskRegistry::instance();

  // Create batch with ids [10, 20, 30, 40]
  auto batch = std::make_shared<ColumnBatch>(4);
  for (size_t i = 0; i < 4; ++i) {
    batch->setId(i, static_cast<int64_t>((i + 1) * 10));
  }

  // selection=[0, 2] (indices 1, 3 filtered out)
  // order=[3, 2, 1, 0] (reverse)
  // Effective iteration: [2, 0] (3 and 1 filtered out by selection)
  RowSet input = RowSet(batch)
                     .withSelection(SelectionVector{0, 2})
                     .withOrder(Permutation{3, 2, 1, 0});

  SECTION("take(1) yields first in iteration order") {
    nlohmann::json take_params_json;
    take_params_json["count"] = 1;
    auto take_params = registry.validate_params("take", take_params_json);
    RowSet result = registry.execute("take", {input}, take_params, empty_ctx);

    REQUIRE(result.batchPtr().get() == input.batchPtr().get());

    auto indices = result.materializeIndexViewForOutput(result.batch().size());
    REQUIRE(indices.size() == 1);
    REQUIRE(indices[0] == 2);
    REQUIRE(result.batch().getId(indices[0]) == 30);
  }

  SECTION("take(2) yields both in iteration order") {
    nlohmann::json take_params_json;
    take_params_json["count"] = 2;
    auto take_params = registry.validate_params("take", take_params_json);
    RowSet result = registry.execute("take", {input}, take_params, empty_ctx);

    auto indices = result.materializeIndexViewForOutput(result.batch().size());
    REQUIRE(indices == std::vector<RowIndex>{2, 0});
  }
}

TEST_CASE("ActiveRows forEachIndex iterates correctly", "[rowset]") {
  auto batch = std::make_shared<ColumnBatch>(5);
  for (size_t i = 0; i < 5; ++i) {
    batch->setId(i, static_cast<int64_t>(i));
  }

  SECTION("collects all indices via forEachIndex") {
    RowSet rs = RowSet(batch).withSelection(SelectionVector{1, 3, 4});

    std::vector<RowIndex> collected;
    rs.activeRows().forEachIndex([&](RowIndex idx) { collected.push_back(idx); });

    REQUIRE(collected == std::vector<RowIndex>{1, 3, 4});
  }

  SECTION("size() matches actual count") {
    RowSet rs = RowSet(batch)
                    .withSelection(SelectionVector{0, 1, 2, 3, 4})
                    .withOrder(Permutation{4, 2, 0}); // Only 3 active after order filter

    REQUIRE(rs.activeRows().size() == 3);
    REQUIRE(rs.logicalSize() == 3);
  }
}

TEST_CASE("RowSet truncateTo works correctly", "[rowset]") {
  auto batch = std::make_shared<ColumnBatch>(10);
  for (size_t i = 0; i < 10; ++i) {
    batch->setId(i, static_cast<int64_t>(i * 10));
  }

  SECTION("truncateTo from full batch") {
    RowSet rs(batch);
    RowSet truncated = rs.truncateTo(3);

    auto indices = truncated.materializeIndexViewForOutput(100);
    REQUIRE(indices == std::vector<RowIndex>{0, 1, 2});
    REQUIRE(truncated.logicalSize() == 3);
  }

  SECTION("truncateTo preserves iteration order") {
    RowSet rs = RowSet(batch).withOrder(Permutation{9, 7, 5, 3, 1});
    RowSet truncated = rs.truncateTo(3);

    auto indices = truncated.materializeIndexViewForOutput(100);
    REQUIRE(indices == std::vector<RowIndex>{9, 7, 5});
  }

  SECTION("truncateTo shares batch pointer") {
    RowSet rs(batch);
    RowSet truncated = rs.truncateTo(5);

    REQUIRE(truncated.batchPtr().get() == rs.batchPtr().get());
  }
}
