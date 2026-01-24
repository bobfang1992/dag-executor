#include "async_dag_scheduler.h"

#include "cpu_offload.h"
#include "output_contract.h"
#include "pred_eval.h"  // For clearRegexCache
#include "schema_delta.h"

#include <atomic>
#include <optional>
#include <queue>

namespace ranking {

namespace {

// Forward declaration
struct AsyncSchedulerState;

// Run a single node and handle completion
Task<void> run_node_async(AsyncSchedulerState& state, size_t node_idx);

// Spawn newly ready nodes
void spawn_ready_nodes(AsyncSchedulerState& state);

/**
 * Async scheduler state - all mutable state for one plan execution.
 *
 * Unlike the sync scheduler, no mutex needed: all operations happen on
 * the EventLoop thread. Coroutines may suspend (on IO/CPU offload) but
 * never run concurrently with each other.
 */
struct AsyncSchedulerState {
  // Immutable after init
  const rankd::Plan& plan;
  const ExecCtxAsync& base_ctx;
  const rankd::TaskRegistry& registry;
  std::unordered_map<std::string, size_t> node_index;    // node_id -> index
  std::vector<std::vector<size_t>> successors;           // node_idx -> [succ indices]
  std::vector<size_t> topo_order;                        // for deterministic output

  // Mutable state (single-threaded, no locks needed)
  std::vector<int> deps_remaining;                       // countdown to 0
  std::vector<std::optional<rankd::RowSet>> results;     // output per node
  std::vector<std::optional<rankd::NodeSchemaDelta>> schema_deltas;  // per node
  size_t num_nodes = 0;
  size_t nodes_remaining = 0;                            // countdown to 0 for completion
  std::queue<size_t> ready_queue;                        // nodes ready to run
  std::optional<std::string> first_error;                // fail-fast

  // Coroutine ownership - keeps Task alive until complete
  std::vector<std::optional<Task<void>>> node_tasks;

  // Main coroutine handle - resumed when all nodes complete
  std::coroutine_handle<> main_coro;

  AsyncSchedulerState(const rankd::Plan& p, const ExecCtxAsync& c, const rankd::TaskRegistry& r)
      : plan(p), base_ctx(c), registry(r) {}
};

void init_async_scheduler_state(AsyncSchedulerState& state) {
  size_t n = state.plan.nodes.size();
  state.num_nodes = n;
  state.nodes_remaining = n;

  // Build node_id -> index map
  for (size_t i = 0; i < n; ++i) {
    state.node_index[state.plan.nodes[i].node_id] = i;
  }

  // Initialize containers
  state.deps_remaining.resize(n, 0);
  state.successors.resize(n);
  state.results.resize(n);
  state.schema_deltas.resize(n);
  state.node_tasks.resize(n);

  // Compute indegrees and build successor edges
  for (size_t i = 0; i < n; ++i) {
    const auto& node = state.plan.nodes[i];

    // Collect all dependencies
    std::vector<std::string> deps = node.inputs;

    // Add NodeRef params as dependencies
    const auto& spec = state.registry.get_spec(node.op);
    for (const auto& field : spec.params_schema) {
      if (field.type == rankd::TaskParamType::NodeRef &&
          node.params.contains(field.name) &&
          node.params[field.name].is_string()) {
        deps.push_back(node.params[field.name].get<std::string>());
      }
    }

    state.deps_remaining[i] = static_cast<int>(deps.size());

    // Build successor edges
    for (const auto& dep_id : deps) {
      size_t parent_idx = state.node_index.at(dep_id);
      state.successors[parent_idx].push_back(i);
    }
  }

  // Compute topo order (for deterministic schema_deltas output)
  std::vector<int> in_degree = state.deps_remaining;

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
    if (state.deps_remaining[i] == 0) {
      state.ready_queue.push(i);
    }
  }
}

/**
 * Awaitable that suspends until all nodes complete.
 *
 * If already complete (nodes_remaining == 0), returns immediately.
 * Otherwise, stores the main coroutine handle and suspends.
 * Resumed by the last completing node.
 */
struct CompletionAwaitable {
  AsyncSchedulerState& state;

  bool await_ready() const noexcept {
    return state.nodes_remaining == 0 || state.first_error.has_value();
  }

  void await_suspend(std::coroutine_handle<> h) noexcept {
    state.main_coro = h;
  }

  void await_resume() noexcept {}
};

/**
 * Called when a node completes successfully.
 * Updates state and potentially spawns successor nodes.
 */
void on_node_success(AsyncSchedulerState& state, size_t node_idx, rankd::RowSet result,
                     rankd::NodeSchemaDelta delta) {
  // Store result
  state.results[node_idx] = std::move(result);
  state.schema_deltas[node_idx] = std::move(delta);

  // Wake successors
  for (size_t succ_idx : state.successors[node_idx]) {
    if (--state.deps_remaining[succ_idx] == 0) {
      state.ready_queue.push(succ_idx);
    }
  }

  // Spawn newly ready nodes
  spawn_ready_nodes(state);

  // Check completion
  --state.nodes_remaining;
  if (state.nodes_remaining == 0 && state.main_coro) {
    state.main_coro.resume();
  }
}

/**
 * Called when a node fails.
 * Records the error and signals completion (fail-fast).
 */
void on_node_failure(AsyncSchedulerState& state, const std::string& error) {
  if (!state.first_error) {
    state.first_error = error;
  }

  // Decrement remaining but don't spawn new nodes
  --state.nodes_remaining;

  // Resume main if this was the last inflight node
  // (other nodes may still be running, but we'll fail-fast when they complete)
  if (state.main_coro) {
    state.main_coro.resume();
  }
}

/**
 * Run a single DAG node as a coroutine.
 *
 * For tasks with runAsync: calls runAsync directly
 * For tasks without runAsync: wraps sync run() with OffloadCpu
 */
Task<void> run_node_async(AsyncSchedulerState& state, size_t node_idx) {
  // Clear thread-local regex cache
  rankd::clearRegexCache();

  try {
    const auto& node = state.plan.nodes[node_idx];

    // 1. Gather inputs from completed parent nodes
    std::vector<rankd::RowSet> inputs;
    for (const auto& parent_id : node.inputs) {
      size_t parent_idx = state.node_index.at(parent_id);
      inputs.push_back(*state.results[parent_idx]);
    }

    // 2. Validate params
    auto validated = state.registry.validate_params(node.op, node.params);

    // 3. Resolve NodeRef params
    std::unordered_map<std::string, rankd::RowSet> resolved_refs;
    for (const auto& [param_name, ref_node_id] : validated.node_ref_params) {
      size_t ref_idx = state.node_index.at(ref_node_id);
      resolved_refs.emplace(param_name, *state.results[ref_idx]);
    }

    // 4. Build execution context for this node
    ExecCtxAsync ctx = state.base_ctx;
    ctx.resolved_node_refs = resolved_refs.empty() ? nullptr : &resolved_refs;

    // 5. Execute the task
    const auto& spec = state.registry.get_spec(node.op);

    // Execute async or sync (wrapped with OffloadCpu)
    auto run_task = [&]() -> Task<rankd::RowSet> {
      if (spec.run_async) {
        // Task has native async implementation
        co_return co_await spec.run_async(inputs, validated, ctx);
      } else {
        // Wrap sync run() with OffloadCpu
        co_return co_await OffloadCpu(*ctx.loop, [&]() {
          // Build sync ExecCtx from async context
          rankd::ExecCtx sync_ctx;
          sync_ctx.params = ctx.params;
          sync_ctx.expr_table = ctx.expr_table;
          sync_ctx.pred_table = ctx.pred_table;
          sync_ctx.stats = ctx.stats;
          sync_ctx.resolved_node_refs = ctx.resolved_node_refs;
          sync_ctx.request = ctx.request;
          sync_ctx.endpoints = ctx.endpoints;
          sync_ctx.clients = nullptr;  // Sync clients not available in async path
          sync_ctx.parallel = false;

          return state.registry.execute(node.op, inputs, validated, sync_ctx);
        });
      }
    };

    rankd::RowSet output = co_await run_task();

    // 6. Validate output contract
    std::vector<rankd::RowSet> contract_inputs = inputs;
    if (spec.output_pattern == rankd::OutputPattern::ConcatDense && !resolved_refs.empty()) {
      contract_inputs.push_back(resolved_refs.at("rhs"));
    }
    rankd::validateTaskOutput(node.node_id, node.op, spec.output_pattern, contract_inputs,
                               validated, output);

    // 7. Compute schema delta
    rankd::NodeSchemaDelta node_delta;
    node_delta.node_id = node.node_id;
    if (!rankd::is_same_batch(contract_inputs, output)) {
      node_delta.delta = rankd::compute_schema_delta(contract_inputs, output);
    } else {
      node_delta.delta.in_keys_union = rankd::collect_keys(contract_inputs[0].batch());
      node_delta.delta.out_keys = node_delta.delta.in_keys_union;
    }

    // 8. Signal completion
    on_node_success(state, node_idx, std::move(output), std::move(node_delta));

  } catch (const std::exception& e) {
    on_node_failure(state, e.what());
  }

  co_return;
}

/**
 * Spawn coroutines for all nodes in the ready queue.
 */
void spawn_ready_nodes(AsyncSchedulerState& state) {
  while (!state.ready_queue.empty() && !state.first_error) {
    size_t node_idx = state.ready_queue.front();
    state.ready_queue.pop();

    // Create and start the node coroutine
    state.node_tasks[node_idx] = run_node_async(state, node_idx);
    state.node_tasks[node_idx]->start();
  }
}

}  // namespace

Task<rankd::ExecutionResult> execute_plan_async(const rankd::Plan& plan, const ExecCtxAsync& ctx) {
  const auto& registry = rankd::TaskRegistry::instance();

  AsyncSchedulerState state(plan, ctx, registry);
  init_async_scheduler_state(state);

  // Spawn initial ready nodes
  spawn_ready_nodes(state);

  // Wait for all nodes to complete (or first error)
  co_await CompletionAwaitable{state};

  // Check for errors
  if (state.first_error) {
    throw std::runtime_error(*state.first_error);
  }

  // Build result
  rankd::ExecutionResult result;

  // Collect outputs
  for (const auto& out_id : plan.outputs) {
    size_t idx = state.node_index.at(out_id);
    result.outputs.push_back(*state.results[idx]);
  }

  // Collect schema_deltas in topo order
  for (size_t idx : state.topo_order) {
    if (state.schema_deltas[idx]) {
      result.schema_deltas.push_back(std::move(*state.schema_deltas[idx]));
    }
  }

  co_return result;
}

rankd::ExecutionResult execute_plan_async_blocking(
    const rankd::Plan& plan,
    EventLoop& loop,
    AsyncIoClients& async_clients,
    const rankd::ParamTable& params,
    const std::unordered_map<std::string, rankd::ExprNodePtr>& expr_table,
    const std::unordered_map<std::string, rankd::PredNodePtr>& pred_table,
    const rankd::EndpointRegistry& endpoints,
    const rankd::RequestContext& request,
    rankd::ExecStats* stats) {

  // Build async context
  ExecCtxAsync ctx;
  ctx.params = &params;
  ctx.expr_table = &expr_table;
  ctx.pred_table = &pred_table;
  ctx.stats = stats;
  ctx.request = &request;
  ctx.endpoints = &endpoints;
  ctx.loop = &loop;
  ctx.async_clients = &async_clients;

  // Result storage
  std::optional<rankd::ExecutionResult> result;
  std::exception_ptr error;
  bool done = false;
  std::mutex done_mutex;
  std::condition_variable done_cv;

  // Wrapper coroutine that signals completion
  auto wrapper = [&]() -> Task<void> {
    try {
      result = co_await execute_plan_async(plan, ctx);
    } catch (...) {
      error = std::current_exception();
    }

    // Signal completion (must Post to event loop thread for safety)
    loop.Post([&]() {
      std::lock_guard<std::mutex> lock(done_mutex);
      done = true;
      done_cv.notify_one();
    });
  };

  // Start the wrapper coroutine on the event loop
  Task<void> task = wrapper();
  loop.Post([&task]() { task.start(); });

  // Wait for completion
  {
    std::unique_lock<std::mutex> lock(done_mutex);
    done_cv.wait(lock, [&] { return done; });
  }

  // Propagate errors
  if (error) {
    std::rethrow_exception(error);
  }

  return std::move(*result);
}

}  // namespace ranking
