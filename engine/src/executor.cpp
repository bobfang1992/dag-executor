#include "executor.h"
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace rankd {

void validate_plan(const Plan &plan) {
  // Check schema_version
  if (plan.schema_version != 1) {
    throw std::runtime_error(
        "Unsupported schema_version: " + std::to_string(plan.schema_version) +
        " (expected 1)");
  }

  // Build node_id -> index map and check uniqueness
  std::unordered_map<std::string, size_t> node_index;
  for (size_t i = 0; i < plan.nodes.size(); ++i) {
    const auto &node = plan.nodes[i];
    if (node_index.find(node.node_id) != node_index.end()) {
      throw std::runtime_error("Duplicate node_id: " + node.node_id);
    }
    node_index[node.node_id] = i;
  }

  // Check all inputs exist, ops are known, and params are valid
  const auto &registry = TaskRegistry::instance();
  for (const auto &node : plan.nodes) {
    if (!registry.has_task(node.op)) {
      throw std::runtime_error("Unknown op '" + node.op + "' in node '" +
                               node.node_id + "'");
    }
    for (const auto &inp : node.inputs) {
      if (node_index.find(inp) == node_index.end()) {
        throw std::runtime_error("Node '" + node.node_id +
                                 "' references missing input: " + inp);
      }
    }
    // Validate params against TaskSpec (fail-closed)
    try {
      registry.validate_params(node.op, node.params);
    } catch (const std::runtime_error &e) {
      throw std::runtime_error("Node '" + node.node_id + "': " + e.what());
    }
  }

  // Check outputs exist
  for (const auto &out : plan.outputs) {
    if (node_index.find(out) == node_index.end()) {
      throw std::runtime_error("Output references missing node: " + out);
    }
  }

  // Check for cycles using Kahn's algorithm
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> successors;

  for (const auto &node : plan.nodes) {
    if (in_degree.find(node.node_id) == in_degree.end()) {
      in_degree[node.node_id] = 0;
    }
    for (const auto &inp : node.inputs) {
      successors[inp].push_back(node.node_id);
      in_degree[node.node_id]++;
    }
  }

  std::queue<std::string> ready;
  for (const auto &[id, deg] : in_degree) {
    if (deg == 0) {
      ready.push(id);
    }
  }

  size_t processed = 0;
  while (!ready.empty()) {
    std::string curr = ready.front();
    ready.pop();
    processed++;

    for (const auto &succ : successors[curr]) {
      if (--in_degree[succ] == 0) {
        ready.push(succ);
      }
    }
  }

  if (processed != plan.nodes.size()) {
    throw std::runtime_error("Plan contains a cycle");
  }
}

std::vector<RowSet> execute_plan(const Plan &plan) {
  // Build node_id -> index map
  std::unordered_map<std::string, size_t> node_index;
  for (size_t i = 0; i < plan.nodes.size(); ++i) {
    node_index[plan.nodes[i].node_id] = i;
  }

  // Topological sort using Kahn's algorithm
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> successors;

  for (const auto &node : plan.nodes) {
    if (in_degree.find(node.node_id) == in_degree.end()) {
      in_degree[node.node_id] = 0;
    }
    for (const auto &inp : node.inputs) {
      successors[inp].push_back(node.node_id);
      in_degree[node.node_id]++;
    }
  }

  std::queue<std::string> ready;
  for (const auto &[id, deg] : in_degree) {
    if (deg == 0) {
      ready.push(id);
    }
  }

  std::vector<std::string> topo_order;
  while (!ready.empty()) {
    std::string curr = ready.front();
    ready.pop();
    topo_order.push_back(curr);

    for (const auto &succ : successors[curr]) {
      if (--in_degree[succ] == 0) {
        ready.push(succ);
      }
    }
  }

  // Execute in topological order
  const auto &registry = TaskRegistry::instance();
  std::unordered_map<std::string, RowSet> results;

  for (const auto &node_id : topo_order) {
    const auto &node = plan.nodes[node_index[node_id]];

    std::vector<RowSet> inputs;
    for (const auto &inp : node.inputs) {
      inputs.push_back(results[inp]);
    }

    auto validated_params = registry.validate_params(node.op, node.params);
    results[node_id] = registry.execute(node.op, inputs, validated_params);
  }

  // Collect outputs
  std::vector<RowSet> outputs;
  for (const auto &out : plan.outputs) {
    outputs.push_back(results[out]);
  }

  return outputs;
}

} // namespace rankd
