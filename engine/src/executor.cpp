#include "executor.h"
#include "capability_registry.h"
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace rankd {

// Build param_bindings from validated params for writes_effect evaluation.
// Uses ValidatedParams which has defaults applied and types normalized.
static EffectGamma build_param_bindings(const ValidatedParams &params) {
  EffectGamma bindings;

  // Int params → uint32_t (for EffectFromParam, e.g., out_key)
  for (const auto &[name, val] : params.int_params) {
    if (val > 0) {  // key_ids are positive
      bindings[name] = static_cast<uint32_t>(val);
    }
  }

  // String params → std::string (for EffectSwitchEnum)
  for (const auto &[name, val] : params.string_params) {
    bindings[name] = val;
  }

  return bindings;
}

void validate_plan(Plan &plan) {
  // Check schema_version
  if (plan.schema_version != 1) {
    throw std::runtime_error(
        "Unsupported schema_version: " + std::to_string(plan.schema_version) +
        " (expected 1)");
  }

  // RFC0001: Validate all required capabilities are supported
  for (const auto &cap : plan.capabilities_required) {
    if (!capability_is_supported(cap)) {
      throw std::runtime_error("unsupported capability required by plan: " +
                               cap);
    }
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

  // Check all inputs exist, ops are known, params are valid, and evaluate writes
  const auto &registry = TaskRegistry::instance();
  for (size_t i = 0; i < plan.nodes.size(); ++i) {
    auto &node = plan.nodes[i];  // non-const to store writes_eval

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
    ValidatedParams validated_params;
    try {
      validated_params = registry.validate_params(node.op, node.params);
    } catch (const std::runtime_error &e) {
      throw std::runtime_error("Node '" + node.node_id + "': " + e.what());
    }

    // Get TaskSpec for this node
    const auto &spec = registry.get_spec(node.op);

    // Validate ExprId/PredId references against plan tables
    for (const auto &field : spec.params_schema) {
      if (!node.params.contains(field.name) || !node.params[field.name].is_string()) {
        continue;
      }
      std::string ref = node.params[field.name].get<std::string>();

      if (field.type == TaskParamType::ExprId) {
        if (plan.expr_table.find(ref) == plan.expr_table.end()) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "': expr_id '" + ref +
                                   "' not found in expr_table");
        }
      } else if (field.type == TaskParamType::PredId) {
        if (plan.pred_table.find(ref) == plan.pred_table.end()) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "': pred_id '" + ref +
                                   "' not found in pred_table");
        }
      }
    }

    // RFC0005: Evaluate writes contract for this node
    // Use validated_params which has defaults applied and types normalized
    EffectGamma param_bindings = build_param_bindings(validated_params);
    WritesEffectExpr writes_expr = compute_effective_writes(spec);
    WritesEffect writes_result = eval_writes(writes_expr, param_bindings);

    node.writes_eval_kind = writes_result.kind;
    node.writes_eval_keys = writes_result.keys;
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

std::vector<RowSet> execute_plan(const Plan &plan, const ExecCtx &ctx) {
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
      inputs.push_back(results.at(inp));
    }

    auto validated_params = registry.validate_params(node.op, node.params);
    RowSet output = registry.execute(node.op, inputs, validated_params, ctx);

    // Validate output against task's output contract
    const auto &spec = registry.get_spec(node.op);
    validateTaskOutput(node_id, node.op, spec.output_pattern, inputs,
                       validated_params, output);

    results.emplace(node_id, std::move(output));
  }

  // Collect outputs
  std::vector<RowSet> outputs;
  for (const auto &out : plan.outputs) {
    outputs.push_back(results.at(out));
  }

  return outputs;
}

} // namespace rankd
