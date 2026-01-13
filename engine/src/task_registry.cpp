#include "task_registry.h"
#include <stdexcept>
#include <algorithm>

namespace rankd {

TaskRegistry& TaskRegistry::instance() {
    static TaskRegistry reg;
    return reg;
}

TaskRegistry::TaskRegistry() {
    // Register viewer.follow
    register_task("viewer.follow", [](const std::vector<RowSet>& inputs,
                                       const nlohmann::json& params) -> RowSet {
        if (!inputs.empty()) {
            throw std::runtime_error("viewer.follow: expected 0 inputs");
        }
        if (!params.contains("fanout") || !params["fanout"].is_number_integer()) {
            throw std::runtime_error("viewer.follow: 'fanout' must be an integer");
        }
        int fanout = params["fanout"].get<int>();
        if (fanout <= 0) {
            throw std::runtime_error("viewer.follow: 'fanout' must be > 0");
        }

        // Create ColumnBatch with ids 1..fanout
        auto batch = std::make_shared<ColumnBatch>(static_cast<size_t>(fanout));
        for (int i = 0; i < fanout; ++i) {
            batch->setId(static_cast<size_t>(i), i + 1);  // ids are 1-indexed
        }

        return RowSet{.batch = batch, .selection = std::nullopt, .order = std::nullopt};
    });

    // Register take
    register_task("take", [](const std::vector<RowSet>& inputs,
                              const nlohmann::json& params) -> RowSet {
        if (inputs.size() != 1) {
            throw std::runtime_error("take: expected exactly 1 input");
        }
        if (!params.contains("count") || !params["count"].is_number_integer()) {
            throw std::runtime_error("take: 'count' must be an integer");
        }
        int count = params["count"].get<int>();
        if (count <= 0) {
            throw std::runtime_error("take: 'count' must be > 0");
        }

        const auto& input = inputs[0];
        size_t limit = static_cast<size_t>(count);

        // Share the same batch - no column copy!
        RowSet result;
        result.batch = input.batch;

        if (input.order) {
            // Truncate order
            auto new_order = *input.order;
            if (new_order.size() > limit) {
                new_order.resize(limit);
            }
            result.order = std::move(new_order);
            result.selection = input.selection;  // preserve selection if any
        } else if (input.selection) {
            // Truncate selection
            auto new_selection = *input.selection;
            if (new_selection.size() > limit) {
                new_selection.resize(limit);
            }
            result.selection = std::move(new_selection);
            result.order = std::nullopt;
        } else {
            // Create selection [0..min(count, N)-1]
            size_t n = std::min(limit, input.batch->size());
            std::vector<uint32_t> new_selection;
            new_selection.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                new_selection.push_back(static_cast<uint32_t>(i));
            }
            result.selection = std::move(new_selection);
            result.order = std::nullopt;
        }

        return result;
    });
}

void TaskRegistry::register_task(const std::string& op, TaskFn fn) {
    tasks_[op] = std::move(fn);
}

bool TaskRegistry::has_task(const std::string& op) const {
    return tasks_.find(op) != tasks_.end();
}

RowSet TaskRegistry::execute(const std::string& op, const std::vector<RowSet>& inputs,
                              const nlohmann::json& params) const {
    auto it = tasks_.find(op);
    if (it == tasks_.end()) {
        throw std::runtime_error("Unknown op: " + op);
    }
    return it->second(inputs, params);
}

} // namespace rankd
