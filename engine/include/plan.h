#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace rankd {

struct Node {
  std::string node_id;
  std::string op;
  std::vector<std::string> inputs;
  nlohmann::json params;
};

struct Plan {
  int schema_version = 0;
  std::string plan_name;
  std::vector<Node> nodes;
  std::vector<std::string> outputs;
};

// Parse plan from JSON file. Throws std::runtime_error on parse failure.
Plan parse_plan(const std::string &path);

} // namespace rankd
