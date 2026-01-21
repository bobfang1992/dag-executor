#include "executor.h"
#include "capability_registry.h"
#include "endpoint_registry.h"
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

void validate_plan(Plan &plan, const EndpointRegistry *endpoints) {
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

    // Validate ExprId/PredId/NodeRef references
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
      } else if (field.type == TaskParamType::NodeRef) {
        // NodeRef must reference an existing node
        if (node_index.find(ref) == node_index.end()) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "': node_ref '" + field.name +
                                   "' references missing node: " + ref);
        }
      } else if (field.type == TaskParamType::EndpointRef) {
        // EndpointRef must reference an endpoint in the registry
        if (endpoints == nullptr) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "': EndpointRef param '" + field.name +
                                   "' requires EndpointRegistry but none provided");
        }
        const EndpointSpec *ep = endpoints->by_id(ref);
        if (ep == nullptr) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "': endpoint_id '" + ref +
                                   "' not found in EndpointRegistry");
        }
        // Check kind constraint if specified
        if (field.endpoint_kind && ep->kind != *field.endpoint_kind) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "': endpoint '" + ref +
                                   "' has kind '" + std::string(endpoint_kind_to_string(ep->kind)) +
                                   "' but param requires '" +
                                   std::string(endpoint_kind_to_string(*field.endpoint_kind)) + "'");
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
  // Dependencies include both inputs and NodeRef params
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> successors;

  for (const auto &node : plan.nodes) {
    if (in_degree.find(node.node_id) == in_degree.end()) {
      in_degree[node.node_id] = 0;
    }
    // Collect all dependencies: inputs + NodeRef params
    std::vector<std::string> deps = node.inputs;
    const auto &spec = registry.get_spec(node.op);
    for (const auto &field : spec.params_schema) {
      if (field.type == TaskParamType::NodeRef &&
          node.params.contains(field.name) &&
          node.params[field.name].is_string()) {
        deps.push_back(node.params[field.name].get<std::string>());
      }
    }
    for (const auto &dep : deps) {
      successors[dep].push_back(node.node_id);
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

ExecutionResult execute_plan(const Plan &plan, const ExecCtx &base_ctx) {
  ExecutionResult result;

  const auto &registry = TaskRegistry::instance();

  // Build node_id -> index map
  std::unordered_map<std::string, size_t> node_index;
  for (size_t i = 0; i < plan.nodes.size(); ++i) {
    node_index[plan.nodes[i].node_id] = i;
  }

  // Topological sort using Kahn's algorithm
  // Dependencies include both inputs and NodeRef params
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> successors;

  for (const auto &node : plan.nodes) {
    if (in_degree.find(node.node_id) == in_degree.end()) {
      in_degree[node.node_id] = 0;
    }
    // Collect all dependencies: inputs + NodeRef params
    std::vector<std::string> deps = node.inputs;
    const auto &spec = registry.get_spec(node.op);
    for (const auto &field : spec.params_schema) {
      if (field.type == TaskParamType::NodeRef &&
          node.params.contains(field.name) &&
          node.params[field.name].is_string()) {
        deps.push_back(node.params[field.name].get<std::string>());
      }
    }
    for (const auto &dep : deps) {
      successors[dep].push_back(node.node_id);
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
  std::unordered_map<std::string, RowSet> results;

  for (const auto &node_id : topo_order) {
    const auto &node = plan.nodes[node_index[node_id]];

    std::vector<RowSet> inputs;
    for (const auto &inp : node.inputs) {
      inputs.push_back(results.at(inp));
    }

    auto validated_params = registry.validate_params(node.op, node.params);

    // Resolve NodeRef params and build execution context
    std::unordered_map<std::string, RowSet> resolved_node_refs;
    for (const auto &[param_name, ref_node_id] : validated_params.node_ref_params) {
      resolved_node_refs.emplace(param_name, results.at(ref_node_id));
    }

    // Create execution context with resolved NodeRefs
    ExecCtx ctx = base_ctx;
    ctx.resolved_node_refs = resolved_node_refs.empty() ? nullptr : &resolved_node_refs;

    RowSet output = registry.execute(node.op, inputs, validated_params, ctx);

    // Validate output against task's output contract
    const auto &spec = registry.get_spec(node.op);
    // For ConcatDense, we need to provide the rhs RowSet as a virtual input
    std::vector<RowSet> contract_inputs = inputs;
    if (spec.output_pattern == OutputPattern::ConcatDense && !resolved_node_refs.empty()) {
      // Add resolved rhs to contract inputs for validation
      contract_inputs.push_back(resolved_node_refs.at("rhs"));
    }
    validateTaskOutput(node_id, node.op, spec.output_pattern, contract_inputs,
                       validated_params, output);

    // RFC0005: Compute schema delta for this node (runtime audit)
    // Use contract_inputs (which includes resolved NodeRefs) for schema delta
    // Fast path: if unary op with same batch pointer, schema is unchanged
    if (!is_same_batch(contract_inputs, output)) {
      SchemaDelta delta = compute_schema_delta(contract_inputs, output);
      result.schema_deltas.push_back({node_id, delta});
    } else {
      // Same batch: no schema change
      SchemaDelta delta;
      delta.in_keys_union = collect_keys(contract_inputs[0].batch());
      delta.out_keys = delta.in_keys_union;
      // new_keys and removed_keys remain empty
      result.schema_deltas.push_back({node_id, delta});
    }

    results.emplace(node_id, std::move(output));
  }

  // Collect outputs
  for (const auto &out : plan.outputs) {
    result.outputs.push_back(results.at(out));
  }

  return result;
}

} // namespace rankd
