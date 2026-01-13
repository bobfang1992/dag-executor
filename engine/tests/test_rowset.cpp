#include <iostream>
#include <cassert>
#include <vector>
#include "column_batch.h"
#include "rowset.h"
#include "task_registry.h"

using namespace rankd;

void test_viewer_follow_and_take() {
    std::cout << "Test: viewer.follow -> take" << std::endl;

    auto& registry = TaskRegistry::instance();

    // viewer.follow with fanout=10
    nlohmann::json follow_params;
    follow_params["fanout"] = 10;
    RowSet source = registry.execute("viewer.follow", {}, follow_params);

    assert(source.batch->size() == 10);
    assert(!source.selection.has_value());
    assert(!source.order.has_value());

    // Verify ids are 1..10
    for (size_t i = 0; i < 10; ++i) {
        assert(source.batch->getId(i) == static_cast<int64_t>(i + 1));
    }

    // take with count=5
    nlohmann::json take_params;
    take_params["count"] = 5;
    RowSet result = registry.execute("take", {source}, take_params);

    // Same batch pointer (no copy)
    assert(result.batch.get() == source.batch.get());

    // Selection should be [0,1,2,3,4]
    assert(result.selection.has_value());
    assert(result.selection->size() == 5);

    // Get output ids
    auto indices = result.materializeIndexViewForOutput(result.batch->size());
    assert(indices.size() == 5);

    std::vector<int64_t> output_ids;
    for (uint32_t idx : indices) {
        output_ids.push_back(result.batch->getId(idx));
    }

    std::vector<int64_t> expected_ids = {1, 2, 3, 4, 5};
    assert(output_ids == expected_ids);

    // Materialize count must be 0
    assert(result.batch->debug()->materialize_count == 0);

    std::cout << "  PASS: output ids are [1,2,3,4,5]" << std::endl;
    std::cout << "  PASS: same batch pointer" << std::endl;
    std::cout << "  PASS: materialize_count == 0" << std::endl;
}

void test_selection_and_order_combined() {
    std::cout << "Test: selection + order combined iteration" << std::endl;

    // Create batch with size=6, ids = [10, 20, 30, 40, 50, 60]
    auto debug = std::make_shared<DebugCounters>();
    auto batch = std::make_shared<ColumnBatch>(6, debug);
    for (size_t i = 0; i < 6; ++i) {
        batch->setId(i, static_cast<int64_t>((i + 1) * 10));
    }

    // selection = [0, 2, 3, 5] (indices 1 and 4 are filtered out)
    // order = [5, 4, 3, 2, 1, 0] (reverse order)
    // Expected iteration: order filtered by selection -> [5, 3, 2, 0]
    RowSet rs;
    rs.batch = batch;
    rs.selection = std::vector<uint32_t>{0, 2, 3, 5};
    rs.order = std::vector<uint32_t>{5, 4, 3, 2, 1, 0};

    auto indices = rs.materializeIndexViewForOutput(batch->size());

    std::vector<uint32_t> expected_indices = {5, 3, 2, 0};
    assert(indices == expected_indices);

    // Verify the ids at those indices
    std::vector<int64_t> output_ids;
    for (uint32_t idx : indices) {
        output_ids.push_back(batch->getId(idx));
    }
    std::vector<int64_t> expected_ids = {60, 40, 30, 10};
    assert(output_ids == expected_ids);

    assert(debug->materialize_count == 0);

    std::cout << "  PASS: iteration yields indices [5,3,2,0]" << std::endl;
    std::cout << "  PASS: output ids are [60,40,30,10]" << std::endl;
    std::cout << "  PASS: materialize_count == 0" << std::endl;
}

void test_order_only() {
    std::cout << "Test: order only iteration" << std::endl;

    auto batch = std::make_shared<ColumnBatch>(4);
    for (size_t i = 0; i < 4; ++i) {
        batch->setId(i, static_cast<int64_t>(i + 1));
    }

    RowSet rs;
    rs.batch = batch;
    rs.order = std::vector<uint32_t>{3, 1, 2, 0};

    auto indices = rs.materializeIndexViewForOutput(batch->size());
    std::vector<uint32_t> expected = {3, 1, 2, 0};
    assert(indices == expected);

    std::cout << "  PASS: order-only iteration works" << std::endl;
}

void test_no_selection_no_order() {
    std::cout << "Test: no selection, no order" << std::endl;

    auto batch = std::make_shared<ColumnBatch>(3);
    for (size_t i = 0; i < 3; ++i) {
        batch->setId(i, static_cast<int64_t>(i + 100));
    }

    RowSet rs;
    rs.batch = batch;

    auto indices = rs.materializeIndexViewForOutput(batch->size());
    std::vector<uint32_t> expected = {0, 1, 2};
    assert(indices == expected);

    std::cout << "  PASS: default iteration [0..N) works" << std::endl;
}

int main() {
    std::cout << "=== RowSet Tests ===" << std::endl;

    test_viewer_follow_and_take();
    test_selection_and_order_combined();
    test_order_only();
    test_no_selection_no_order();

    std::cout << std::endl << "All tests passed!" << std::endl;
    return 0;
}
