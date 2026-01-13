#include "plan.h"
#include <fstream>
#include <stdexcept>

namespace rankd {

Plan parse_plan(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot open plan file: " + path);
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(file);
  } catch (const nlohmann::json::parse_error &e) {
    throw std::runtime_error("Invalid JSON in plan file: " +
                             std::string(e.what()));
  }

  Plan plan;

  // schema_version
  if (!j.contains("schema_version") ||
      !j["schema_version"].is_number_integer()) {
    throw std::runtime_error("Plan missing or invalid 'schema_version'");
  }
  plan.schema_version = j["schema_version"].get<int>();

  // plan_name
  if (!j.contains("plan_name") || !j["plan_name"].is_string()) {
    throw std::runtime_error("Plan missing or invalid 'plan_name'");
  }
  plan.plan_name = j["plan_name"].get<std::string>();

  // nodes
  if (!j.contains("nodes") || !j["nodes"].is_array()) {
    throw std::runtime_error("Plan missing or invalid 'nodes'");
  }
  for (const auto &nj : j["nodes"]) {
    Node node;
    if (!nj.contains("node_id") || !nj["node_id"].is_string()) {
      throw std::runtime_error("Node missing or invalid 'node_id'");
    }
    node.node_id = nj["node_id"].get<std::string>();

    if (!nj.contains("op") || !nj["op"].is_string()) {
      throw std::runtime_error("Node '" + node.node_id +
                               "' missing or invalid 'op'");
    }
    node.op = nj["op"].get<std::string>();

    if (!nj.contains("inputs") || !nj["inputs"].is_array()) {
      throw std::runtime_error("Node '" + node.node_id +
                               "' missing or invalid 'inputs'");
    }
    for (const auto &inp : nj["inputs"]) {
      if (!inp.is_string()) {
        throw std::runtime_error("Node '" + node.node_id +
                                 "' has non-string input");
      }
      node.inputs.push_back(inp.get<std::string>());
    }

    if (nj.contains("params")) {
      node.params = nj["params"];
    } else {
      node.params = nlohmann::json::object();
    }

    plan.nodes.push_back(std::move(node));
  }

  // outputs
  if (!j.contains("outputs") || !j["outputs"].is_array()) {
    throw std::runtime_error("Plan missing or invalid 'outputs'");
  }
  for (const auto &out : j["outputs"]) {
    if (!out.is_string()) {
      throw std::runtime_error("Plan has non-string output");
    }
    plan.outputs.push_back(out.get<std::string>());
  }

  return plan;
}

} // namespace rankd
