#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "async_io_clients.h"
#include "coro_task.h"
#include "dag_scheduler.h"  // For ExecutionResult
#include "endpoint_registry.h"
#include "event_loop.h"
#include "param_table.h"  // For ParamTable, ExprNodePtr, PredNodePtr, ExecStats
#include "plan.h"
#include "request.h"
#include "rowset.h"
#include "task_registry.h"

namespace ranking {

/**
 * Async execution context passed to task runAsync functions.
 *
 * Similar to rankd::ExecCtx but includes async-specific resources:
 * - EventLoop reference for async operations
 * - AsyncIoClients for async Redis access
 * - Process-level client cache (shared across requests for proper inflight limiting)
 *
 * Thread model: All async operations happen on the EventLoop thread.
 * CPU-bound work is offloaded via OffloadCpu.
 */
struct ExecCtxAsync {
  // Plan/param tables (shared, read-only)
  const rankd::ParamTable* params = nullptr;
  const std::unordered_map<std::string, rankd::ExprNodePtr>* expr_table = nullptr;
  const std::unordered_map<std::string, rankd::PredNodePtr>* pred_table = nullptr;

  // Statistics tracking (optional)
  rankd::ExecStats* stats = nullptr;

  // Resolved NodeRef params: param_name -> RowSet from referenced node
  const std::unordered_map<std::string, rankd::RowSet>* resolved_node_refs = nullptr;

  // Request context (user_id, request_id, etc.)
  const rankd::RequestContext* request = nullptr;

  // Endpoint registry for IO configuration lookup
  const rankd::EndpointRegistry* endpoints = nullptr;

  // Async-specific: EventLoop for coroutine scheduling
  EventLoop* loop = nullptr;

  // Async-specific: Process-level async client cache
  // Shared across all requests on this EventLoop for proper inflight limiting
  AsyncIoClients* async_clients = nullptr;
};

/**
 * Async task function signature.
 *
 * Tasks that implement runAsync can use co_await for:
 * - Redis operations via AsyncRedisClient
 * - Sleep/timer operations via SleepMs
 * - CPU work offloading via OffloadCpu
 *
 * If a task doesn't implement runAsync, the scheduler automatically
 * wraps the sync run() with OffloadCpu to keep the event loop responsive.
 */
using AsyncTaskFn = std::function<Task<rankd::RowSet>(
    const std::vector<rankd::RowSet>&, const rankd::ValidatedParams&, const ExecCtxAsync&)>;

/**
 * Execute a DAG plan using the async scheduler.
 *
 * All execution happens on a single EventLoop thread:
 * - DAG scheduling and coordination
 * - IO operations (Redis via AsyncRedisClient)
 * - CPU work offloaded to CPU pool via OffloadCpu
 *
 * Level 2 parallelism (within-request DAG branches) is achieved by:
 * - Launching ready nodes as concurrent coroutines
 * - Nodes suspend on IO (co_await), allowing other nodes to run
 * - CPU work runs in parallel on CPU pool threads
 *
 * @param plan The DAG plan to execute
 * @param ctx Async execution context with EventLoop and AsyncIoClients
 * @return ExecutionResult with outputs and schema deltas
 *
 * MUST be co_awaited from a coroutine running on the EventLoop.
 */
Task<rankd::ExecutionResult> execute_plan_async(const rankd::Plan& plan, const ExecCtxAsync& ctx);

/**
 * Blocking wrapper for execute_plan_async.
 *
 * Starts the EventLoop, runs the plan, and returns the result.
 * Useful for integration with the existing synchronous main().
 *
 * @param plan The DAG plan to execute
 * @param loop EventLoop to use (must be started)
 * @param async_clients Process-level async client cache
 * @param params Parameter table
 * @param expr_table Expression table
 * @param pred_table Predicate table
 * @param endpoints Endpoint registry
 * @param request Request context
 * @param stats Optional execution stats
 * @return ExecutionResult with outputs and schema deltas
 */
rankd::ExecutionResult execute_plan_async_blocking(
    const rankd::Plan& plan,
    EventLoop& loop,
    AsyncIoClients& async_clients,
    const rankd::ParamTable& params,
    const std::unordered_map<std::string, rankd::ExprNodePtr>& expr_table,
    const std::unordered_map<std::string, rankd::PredNodePtr>& pred_table,
    const rankd::EndpointRegistry& endpoints,
    const rankd::RequestContext& request,
    rankd::ExecStats* stats = nullptr);

}  // namespace ranking
