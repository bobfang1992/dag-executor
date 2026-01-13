#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace rankd {

struct RowSet {
    std::vector<int64_t> ids;
};

// Task function signature: (inputs, params) -> output
using TaskFn = std::function<RowSet(const std::vector<RowSet>&, const nlohmann::json&)>;

class TaskRegistry {
public:
    static TaskRegistry& instance();

    void register_task(const std::string& op, TaskFn fn);
    bool has_task(const std::string& op) const;
    RowSet execute(const std::string& op, const std::vector<RowSet>& inputs,
                   const nlohmann::json& params) const;

private:
    TaskRegistry();
    std::unordered_map<std::string, TaskFn> tasks_;
};

} // namespace rankd
