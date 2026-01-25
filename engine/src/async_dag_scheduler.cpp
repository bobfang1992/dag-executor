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

  // Deadline/timeout config
  OptionalDeadline request_deadline;
  std::optional<std::chrono::milliseconds> node_timeout;

  // Mutable state (single-threaded, no locks needed)
  std::vector<int> deps_remaining;                       // countdown to 0
  std::vector<std::optional<rankd::RowSet>> results;     // output per node
  std::vector<std::optional<rankd::NodeSchemaDelta>> schema_deltas;  // per node
  size_t num_nodes = 0;
  size_t nodes_remaining = 0;                            // countdown to 0 for completion
  size_t inflight_count = 0;                             // running coroutines (for safe shutdown)
  std::queue<size_t> ready_queue;                        // nodes ready to run
  std::optional<std::string> first_error;                // fail-fast

  // Coroutine ownership - keeps Task alive until complete
  std::vector<std::optional<Task<void>>> node_tasks;

  // Main coroutine handle - resumed when all nodes complete
  std::coroutine_handle<> main_coro;

  AsyncSchedulerState(const rankd::Plan& p, const ExecCtxAsync& c, const rankd::TaskRegistry& r,
                      OptionalDeadline deadline, std::optional<std::chrono::milliseconds> timeout)
      : plan(p), base_ctx(c), registry(r), request_deadline(deadline), node_timeout(timeout) {}
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
 * Awaitable that suspends until all in-flight tasks complete.
 *
 * IMPORTANT: We must wait for ALL in-flight coroutines, not just check for
 * first_error. If we return early on error while coroutines are still suspended
 * on Redis/OffloadCpu, destroying state causes use-after-free when they resume.
 *
 * Returns immediately only if no tasks were ever started (inflight_count == 0).
 * Otherwise, stores the main coroutine handle and suspends until the last
 * in-flight task decrements inflight_count to 0 and resumes us.
 */
struct CompletionAwaitable {
  AsyncSchedulerState& state;

  bool await_ready() const noexcept {
    // Only ready when ALL in-flight tasks have completed.
    // Do NOT check first_error here - we must wait for suspended coroutines
    // to finish before destroying state, even on error.
    return state.inflight_count == 0;
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

  // Track completion count (main_coro resumed by run_node_async when inflight_count hits 0)
  --state.nodes_remaining;
}

/**
 * Called when a node fails.
 * Records the error (fail-fast: no new nodes spawned).
 * main_coro resumed by run_node_async when inflight_count hits 0.
 */
void on_node_failure(AsyncSchedulerState& state, const std::string& error) {
  if (!state.first_error) {
    state.first_error = error;
  }

  // Decrement remaining but don't spawn new nodes
  --state.nodes_remaining;
}

/**
 * Run a single DAG node as a coroutine.
 *
 * For tasks with runAsync: calls runAsync directly
 * For tasks without runAsync: wraps sync run() with OffloadCpuWithTimeout
 */
Task<void> run_node_async(AsyncSchedulerState& state, size_t node_idx) {
  try {
    const auto& node = state.plan.nodes[node_idx];

    // Capture node start time for deadline computation
    auto start_time = std::chrono::steady_clock::now();

    // Compute effective deadline for this node
    auto effective_deadline = compute_effective_deadline(
        start_time, state.request_deadline, state.node_timeout);

    // Check if deadline already exceeded before we start
    if (deadline_exceeded_at(start_time, effective_deadline)) {
      throw std::runtime_error("Node execution timeout (deadline exceeded before start)");
    }

    // 1. Gather inputs from completed parent nodes
    std::vector<rankd::RowSet> inputs;
    for (const auto& parent_id : node.inputs) {
      size_t parent_idx = state.node_index.at(parent_id);
      inputs.push_back(*state.results[parent_idx]);
    }

    // 2. Validate params
    auto validated = state.registry.validate_params(node.op, node.params);

    // 3. Resolve NodeRef params
    // Use shared_ptr so CPU lambda can safely access even if timeout fires and
    // coroutine frame is destroyed (avoids use-after-free for concat-style nodes)
    auto resolved_refs =
        std::make_shared<std::unordered_map<std::string, rankd::RowSet>>();
    for (const auto& [param_name, ref_node_id] : validated.node_ref_params) {
      size_t ref_idx = state.node_index.at(ref_node_id);
      resolved_refs->emplace(param_name, *state.results[ref_idx]);
    }

    // 4. Build execution context for this node
    ExecCtxAsync ctx = state.base_ctx;
    ctx.resolved_node_refs = resolved_refs->empty() ? nullptr : resolved_refs.get();

    // 5. Execute the task
    const auto& spec = state.registry.get_spec(node.op);

    // Execute async or sync (both wrapped with deadline support)
    auto run_task = [&]() -> Task<rankd::RowSet> {
      if (spec.run_async) {
        // Task has native async implementation - wrap with AsyncWithTimeout
        // IMPORTANT: Copy ALL data that the async task may access after its await.
        // If timeout fires, run_node_async exits and AsyncSchedulerState is destroyed,
        // but the async task continues in the runner until it truly completes.
        // The wrapper coroutine captures shared_ptrs, keeping data alive.
        auto async_inputs = std::make_shared<std::vector<rankd::RowSet>>(inputs);
        auto async_validated = std::make_shared<rankd::ValidatedParams>(validated);

        // Copy ctx data into shared storage (same as CPU path)
        auto params_copy = std::make_shared<rankd::ParamTable>(*ctx.params);
        auto expr_table_copy = std::make_shared<
            std::unordered_map<std::string, rankd::ExprNodePtr>>(*ctx.expr_table);
        auto pred_table_copy = std::make_shared<
            std::unordered_map<std::string, rankd::PredNodePtr>>(*ctx.pred_table);
        auto request_copy = std::make_shared<rankd::RequestContext>(*ctx.request);
        std::shared_ptr<rankd::EndpointRegistry> endpoints_copy =
            ctx.endpoints ? std::make_shared<rankd::EndpointRegistry>(*ctx.endpoints)
                          : nullptr;
        // loop and async_clients must remain valid (owned by caller of execute_plan_async_blocking)
        // resolved_refs is already a shared_ptr, passed to wrapper which captures it in
        // the coroutine frame - keeps map alive even if run_node_async exits on timeout

        // Wrapper coroutine captures all shared_ptrs, keeping data alive
        auto wrapper = [](std::shared_ptr<std::vector<rankd::RowSet>> in,
                          std::shared_ptr<rankd::ValidatedParams> vp,
                          std::shared_ptr<rankd::ParamTable> params,
                          std::shared_ptr<std::unordered_map<std::string, rankd::ExprNodePtr>> expr,
                          std::shared_ptr<std::unordered_map<std::string, rankd::PredNodePtr>> pred,
                          std::shared_ptr<rankd::RequestContext> req,
                          std::shared_ptr<rankd::EndpointRegistry> ep,
                          std::shared_ptr<std::unordered_map<std::string, rankd::RowSet>> refs,
                          AsyncTaskFn run_async_fn,
                          EventLoop* loop,
                          AsyncIoClients* clients) -> Task<rankd::RowSet> {
          // Build async ctx from shared copies
          ranking::ExecCtxAsync async_ctx;
          async_ctx.params = params.get();
          async_ctx.expr_table = expr.get();
          async_ctx.pred_table = pred.get();
          // Note: stats intentionally null for async tasks. On timeout, the task continues
          // as a late completion and we can't reliably attribute timing. Caller handles
          // overall request timing. See also CPU path (sync_ctx.stats = nullptr).
          async_ctx.stats = nullptr;
          async_ctx.resolved_node_refs = refs->empty() ? nullptr : refs.get();
          async_ctx.request = req.get();
          async_ctx.endpoints = ep ? ep.get() : nullptr;
          async_ctx.loop = loop;
          async_ctx.async_clients = clients;

          co_return co_await run_async_fn(*in, *vp, async_ctx);
        };

        co_return co_await AsyncWithTimeout<rankd::RowSet>(
            *ctx.loop, effective_deadline,
            wrapper(async_inputs, async_validated, params_copy, expr_table_copy,
                    pred_table_copy, request_copy, endpoints_copy, resolved_refs,
                    spec.run_async, ctx.loop, ctx.async_clients));
      } else {
        // Wrap sync run() with OffloadCpuWithTimeout for deadline support
        // IMPORTANT: All data must be copied/shared because if timeout fires,
        // the caller may return and destroy stack data while CPU work continues.
        auto& registry = state.registry;
        std::string op = node.op;
        auto captured_inputs = inputs;
        auto captured_validated = validated;

        // Copy ctx data into shared storage so CPU lambda owns it.
        // If timeout fires and caller returns, CPU work can still safely access.
        auto params_copy = std::make_shared<rankd::ParamTable>(*ctx.params);
        auto expr_table_copy = std::make_shared<
            std::unordered_map<std::string, rankd::ExprNodePtr>>(*ctx.expr_table);
        auto pred_table_copy = std::make_shared<
            std::unordered_map<std::string, rankd::PredNodePtr>>(*ctx.pred_table);
        auto request_copy = std::make_shared<rankd::RequestContext>(*ctx.request);
        // endpoints may be null for CPU-only plans (though async scheduler typically requires it)
        std::shared_ptr<rankd::EndpointRegistry> endpoints_copy =
            ctx.endpoints ? std::make_shared<rankd::EndpointRegistry>(*ctx.endpoints)
                          : nullptr;
        // stats is optional and only for timing - skip on timeout (result discarded anyway)
        // resolved_refs is already a shared_ptr

        co_return co_await OffloadCpuWithTimeout(
            *ctx.loop, effective_deadline,
            [&registry, op = std::move(op), captured_inputs = std::move(captured_inputs),
             captured_validated = std::move(captured_validated),
             params_copy, expr_table_copy, pred_table_copy,
             resolved_refs, request_copy, endpoints_copy]() mutable {
              // Clear thread-local regex cache on CPU thread
              rankd::clearRegexCache();

              // Build sync ExecCtx from shared copies
              rankd::ExecCtx sync_ctx;
              sync_ctx.params = params_copy.get();
              sync_ctx.expr_table = expr_table_copy.get();
              sync_ctx.pred_table = pred_table_copy.get();
              sync_ctx.stats = nullptr;  // Skip stats - result may be discarded on timeout
              sync_ctx.resolved_node_refs =
                  resolved_refs->empty() ? nullptr : resolved_refs.get();
              sync_ctx.request = request_copy.get();
              sync_ctx.endpoints = endpoints_copy ? endpoints_copy.get() : nullptr;
              sync_ctx.clients = nullptr;  // Sync clients not available in async path
              sync_ctx.parallel = false;

              return registry.execute(op, captured_inputs, captured_validated,
                                       sync_ctx);
            });
      }
    };

    rankd::RowSet output = co_await run_task();

    // 6. Validate output contract
    std::vector<rankd::RowSet> contract_inputs = inputs;
    if (spec.output_pattern == rankd::OutputPattern::ConcatDense && resolved_refs->contains("rhs")) {
      contract_inputs.push_back(resolved_refs->at("rhs"));
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

  // Decrement inflight count and resume main_coro when last task completes.
  // IMPORTANT: We must Post() the resume rather than calling it directly, because
  // main_coro.resume() will destroy AsyncSchedulerState (including node_tasks),
  // but we're still inside this coroutine! Resuming synchronously would destroy
  // the coroutine that's currently running → use-after-free.
  --state.inflight_count;
  if (state.inflight_count == 0 && state.main_coro) {
    auto main_coro = state.main_coro;
    state.base_ctx.loop->Post([main_coro]() { main_coro.resume(); });
  }

  co_return;
}

/**
 * Spawn coroutines for all nodes in the ready queue.
 */
void spawn_ready_nodes(AsyncSchedulerState& state) {
  // Check request deadline before spawning new nodes
  if (deadline_exceeded(state.request_deadline)) {
    if (!state.first_error) {
      state.first_error = "Request deadline exceeded";
    }
    return;  // Don't spawn new nodes
  }

  while (!state.ready_queue.empty() && !state.first_error) {
    size_t node_idx = state.ready_queue.front();
    state.ready_queue.pop();

    // Track inflight before starting (decremented in run_node_async on completion)
    ++state.inflight_count;

    // Create and start the node coroutine
    state.node_tasks[node_idx] = run_node_async(state, node_idx);
    state.node_tasks[node_idx]->start();
  }
}

}  // namespace

Task<rankd::ExecutionResult> execute_plan_async(
    const rankd::Plan& plan,
    const ExecCtxAsync& ctx,
    OptionalDeadline request_deadline,
    std::optional<std::chrono::milliseconds> node_timeout) {
  const auto& registry = rankd::TaskRegistry::instance();

  AsyncSchedulerState state(plan, ctx, registry, request_deadline, node_timeout);
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
    rankd::ExecStats* stats,
    OptionalDeadline request_deadline,
    std::optional<std::chrono::milliseconds> node_timeout) {

  // Guard: calling from loop thread would deadlock (we'd block waiting for
  // callbacks that can't run because we're blocking the loop thread)
  if (loop.IsLoopThread()) {
    throw std::runtime_error(
        "execute_plan_async_blocking called from loop thread; use execute_plan_async instead");
  }

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
      result = co_await execute_plan_async(plan, ctx, request_deadline, node_timeout);
    } catch (...) {
      error = std::current_exception();
    }

    // Signal completion - we're on loop thread, cv notify is thread-safe
    {
      std::lock_guard<std::mutex> lock(done_mutex);
      done = true;
    }
    done_cv.notify_one();
  };

  // Start the wrapper coroutine on the event loop
  Task<void> task = wrapper();
  if (!loop.Post([&task]() { task.start(); })) {
    throw std::runtime_error("Failed to start async plan execution: EventLoop not running");
  }

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
