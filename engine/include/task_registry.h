#pragma once

#include "rowset.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace rankd {

// Task function signature: (inputs, params) -> output
using TaskFn =
    std::function<RowSet(const std::vector<RowSet> &, const nlohmann::json &)>;

class TaskRegistry {
public:
  static TaskRegistry &instance();

  void register_task(const std::string &op, TaskFn fn);
  bool has_task(const std::string &op) const;
  RowSet execute(const std::string &op, const std::vector<RowSet> &inputs,
                 const nlohmann::json &params) const;

private:
  TaskRegistry();
  std::unordered_map<std::string, TaskFn> tasks_;
};

} // namespace rankd
