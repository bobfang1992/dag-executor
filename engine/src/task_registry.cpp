#include "task_registry.h"
#include <stdexcept>

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

        RowSet result;
        for (int i = 1; i <= fanout; ++i) {
            result.ids.push_back(i);
        }
        return result;
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

        RowSet result;
        const auto& src = inputs[0].ids;
        size_t limit = std::min(static_cast<size_t>(count), src.size());
        for (size_t i = 0; i < limit; ++i) {
            result.ids.push_back(src[i]);
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
