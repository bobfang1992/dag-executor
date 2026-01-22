#pragma once

#include "executor.h"
#include "plan.h"

namespace rankd {

/**
 * Execute a plan with within-request parallelism (Level 2).
 *
 * Runs independent DAG nodes concurrently using the CPU thread pool.
 * The scheduler runs in the caller thread and dispatches node jobs
 * to the pool. Each node job:
 *   1. Gathers inputs from completed parent nodes
 *   2. Executes the task synchronously
 *   3. Stores output and wakes scheduler when successors become ready
 *
 * Thread safety: Assumes ctx.clients is thread-safe (IoClients with mutex).
 *
 * @param plan The validated plan to execute
 * @param ctx Execution context (must have clients, endpoints, etc.)
 * @param max_nodes_inflight Max concurrent node jobs (0 = default based on pool size)
 * @return Execution result with outputs and schema deltas (sorted by topo order)
 * @throws std::runtime_error on node failure (fail-fast)
 */
ExecutionResult execute_plan_parallel(
    const Plan& plan,
    const ExecCtx& ctx,
    int max_nodes_inflight = 0);

}  // namespace rankd
