#include "dag_scheduler.h"

#include "cpu_pool.h"
#include "output_contract.h"
#include "pred_eval.h"  // For clearRegexCache
#include "schema_delta.h"
#include "task_registry.h"
#include "thread_pool.h"  // For GetIOThreadPool

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace rankd {

namespace {

struct SchedulerState {
  // Immutable after init
  const Plan& plan;
  const ExecCtx& base_ctx;
  const TaskRegistry& registry;
  std::unordered_map<std::string, size_t> node_index;    // node_id -> index
  std::vector<std::vector<size_t>> successors;           // node_idx -> [succ indices]
  std::vector<size_t> topo_order;                        // for deterministic output

  // Mutable state (protected by mutex)
  std::unique_ptr<std::atomic<int>[]> deps_remaining;    // countdown to 0
  std::vector<std::optional<RowSet>> results;            // output per node
  std::vector<std::optional<NodeSchemaDelta>> schema_deltas;  // per node
  size_t num_nodes = 0;

  std::mutex mutex;
  std::condition_variable cv;
  std::queue<size_t> ready_queue;                        // nodes ready to run
  std::atomic<int> inflight{0};                          // currently running
  std::optional<std::string> first_error;                // fail-fast

  int max_nodes_inflight;

  SchedulerState(const Plan& p, const ExecCtx& c, const TaskRegistry& r, int max_inflight)
      : plan(p), base_ctx(c), registry(r), max_nodes_inflight(max_inflight) {}
};

void init_scheduler_state(SchedulerState& state) {
  size_t n = state.plan.nodes.size();
  state.num_nodes = n;

  // Build node_id -> index map
  for (size_t i = 0; i < n; ++i) {
    state.node_index[state.plan.nodes[i].node_id] = i;
  }

  // Initialize containers
  state.deps_remaining = std::make_unique<std::atomic<int>[]>(n);
  for (size_t i = 0; i < n; ++i) {
    state.deps_remaining[i].store(0, std::memory_order_relaxed);
  }
  state.successors.resize(n);
  state.results.resize(n);
  state.schema_deltas.resize(n);

  // Compute indegrees and build successor edges
  // Dependencies = inputs + NodeRef params
  for (size_t i = 0; i < n; ++i) {
    const auto& node = state.plan.nodes[i];

    // Collect all dependencies
    std::vector<std::string> deps = node.inputs;

    // Add NodeRef params as dependencies
    const auto& spec = state.registry.get_spec(node.op);
    for (const auto& field : spec.params_schema) {
      if (field.type == TaskParamType::NodeRef &&
          node.params.contains(field.name) &&
          node.params[field.name].is_string()) {
        deps.push_back(node.params[field.name].get<std::string>());
      }
    }

    state.deps_remaining[i].store(static_cast<int>(deps.size()), std::memory_order_relaxed);

    // Build successor edges
    for (const auto& dep_id : deps) {
      size_t parent_idx = state.node_index.at(dep_id);
      state.successors[parent_idx].push_back(i);
    }
  }

  // Compute topo order (for deterministic schema_deltas output)
  // Using Kahn's algorithm
  std::vector<int> in_degree(n);
  for (size_t i = 0; i < n; ++i) {
    in_degree[i] = state.deps_remaining[i].load(std::memory_order_relaxed);
  }

  std::queue<size_t> q;
  for (size_t i = 0; i < n; ++i) {
    if (in_degree[i] == 0) {
      q.push(i);
    }
  }

  while (!q.empty()) {
    size_t curr = q.front();
    q.pop();
    state.topo_order.push_back(curr);

    for (size_t succ : state.successors[curr]) {
      if (--in_degree[succ] == 0) {
        q.push(succ);
      }
    }
  }

  // Push nodes with indegree 0 to ready_queue
  for (size_t i = 0; i < n; ++i) {
    if (state.deps_remaining[i].load(std::memory_order_relaxed) == 0) {
      state.ready_queue.push(i);
    }
  }
}

void run_node_job(SchedulerState& state, size_t node_idx) {
  // Clear thread-local regex cache to avoid stale pointer-based lookups
  // (each request may reuse dict pointers for different dictionaries)
  clearRegexCache();

  try {
    const auto& node = state.plan.nodes[node_idx];

    // 1. Gather inputs from completed parent nodes
    std::vector<RowSet> inputs;
    for (const auto& parent_id : node.inputs) {
      size_t parent_idx = state.node_index.at(parent_id);
      // Parent is guaranteed complete (deps_remaining was 0)
      inputs.push_back(*state.results[parent_idx]);
    }

    // 2. Validate params
    auto validated = state.registry.validate_params(node.op, node.params);

    // 3. Resolve NodeRef params (e.g., concat's rhs)
    std::unordered_map<std::string, RowSet> resolved_refs;
    for (const auto& [param_name, ref_node_id] : validated.node_ref_params) {
      size_t ref_idx = state.node_index.at(ref_node_id);
      resolved_refs.emplace(param_name, *state.results[ref_idx]);
    }

    // 4. Build execution context for this node
    ExecCtx ctx = state.base_ctx;
    ctx.resolved_node_refs = resolved_refs.empty() ? nullptr : &resolved_refs;

    // 5. Execute the task
    RowSet output = state.registry.execute(node.op, inputs, validated, ctx);

    // 6. Validate output contract
    const auto& spec = state.registry.get_spec(node.op);
    std::vector<RowSet> contract_inputs = inputs;
    if (spec.output_pattern == OutputPattern::ConcatDense && resolved_refs.contains("rhs")) {
      contract_inputs.push_back(resolved_refs.at("rhs"));
    }
    validateTaskOutput(node.node_id, node.op, spec.output_pattern, contract_inputs,
                       validated, output);

    // 7. Compute schema delta
    NodeSchemaDelta node_delta;
    node_delta.node_id = node.node_id;
    if (!is_same_batch(contract_inputs, output)) {
      node_delta.delta = compute_schema_delta(contract_inputs, output);
    } else {
      // Same batch: no schema change
      node_delta.delta.in_keys_union = collect_keys(contract_inputs[0].batch());
      node_delta.delta.out_keys = node_delta.delta.in_keys_union;
    }

    // 8. Store result and update successors
    {
      std::lock_guard<std::mutex> lock(state.mutex);

      state.results[node_idx] = std::move(output);
      state.schema_deltas[node_idx] = std::move(node_delta);

      // Decrement deps for each successor
      for (size_t succ_idx : state.successors[node_idx]) {
        int prev = state.deps_remaining[succ_idx].fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
          // Was 1, now 0 -> successor is ready
          state.ready_queue.push(succ_idx);
        }
      }

      state.inflight.fetch_sub(1, std::memory_order_acq_rel);
    }
    state.cv.notify_one();

  } catch (const std::exception& e) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.first_error) {
      state.first_error = e.what();
    }
    state.inflight.fetch_sub(1, std::memory_order_acq_rel);
    state.cv.notify_one();
  }
}

}  // namespace

ExecutionResult execute_plan_parallel(
    const Plan& plan,
    const ExecCtx& ctx,
    int max_nodes_inflight) {

  const auto& registry = TaskRegistry::instance();
  auto& cpu_pool = GetCPUThreadPool();

  // Default max_nodes_inflight to pool size
  if (max_nodes_inflight <= 0) {
    max_nodes_inflight = static_cast<int>(cpu_pool.size());
  }

  SchedulerState state(plan, ctx, registry, max_nodes_inflight);
  init_scheduler_state(state);

  // Main scheduler loop (runs in caller thread)
  while (true) {
    std::unique_lock<std::mutex> lock(state.mutex);

    // Check for error (fail-fast)
    if (state.first_error) {
      // Wait for all inflight nodes to complete
      state.cv.wait(lock, [&] {
        return state.inflight.load(std::memory_order_acquire) == 0;
      });
      throw std::runtime_error(*state.first_error);
    }

    // Dispatch ready nodes up to max_nodes_inflight
    while (!state.ready_queue.empty() &&
           state.inflight.load(std::memory_order_acquire) < state.max_nodes_inflight) {
      size_t node_idx = state.ready_queue.front();
      state.ready_queue.pop();
      state.inflight.fetch_add(1, std::memory_order_acq_rel);

      // Check if this is an IO task to dispatch to the appropriate pool
      const auto& node = state.plan.nodes[node_idx];
      const auto& spec = state.registry.get_spec(node.op);
      bool is_io = spec.is_io;

      lock.unlock();
      if (is_io) {
        // Dispatch IO tasks to IO pool (blocking Redis calls)
        GetIOThreadPool().submit([&state, node_idx] {
          run_node_job(state, node_idx);
        });
      } else {
        // Dispatch compute tasks to CPU pool
        cpu_pool.submit([&state, node_idx] {
          run_node_job(state, node_idx);
        });
      }
      lock.lock();
    }

    // Check if done
    if (state.ready_queue.empty() &&
        state.inflight.load(std::memory_order_acquire) == 0) {
      break;  // All nodes complete
    }

    // Wait for a node to complete
    state.cv.wait(lock);
  }

  // Build result
  ExecutionResult result;

  // Collect outputs (copy instead of move to handle duplicate output IDs)
  for (const auto& out_id : plan.outputs) {
    size_t idx = state.node_index.at(out_id);
    result.outputs.push_back(*state.results[idx]);
  }

  // Collect schema_deltas in topo order (deterministic)
  for (size_t idx : state.topo_order) {
    if (state.schema_deltas[idx]) {
      result.schema_deltas.push_back(std::move(*state.schema_deltas[idx]));
    }
  }

  return result;
}

}  // namespace rankd
