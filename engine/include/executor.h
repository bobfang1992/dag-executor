#pragma once

#include "param_table.h"
#include "plan.h"
#include "schema_delta.h"
#include "task_registry.h"
#include <vector>

namespace rankd {

// Validate plan (fail-closed). Throws std::runtime_error on failure.
// Also populates node.writes_eval_kind and node.writes_eval_keys (RFC0005).
void validate_plan(Plan &plan);

// Result of executing a plan, including optional trace data
struct ExecutionResult {
  std::vector<RowSet> outputs;
  std::vector<NodeSchemaDelta> schema_deltas;  // RFC0005: per-node schema changes
};

// Execute plan and return results with schema delta trace.
ExecutionResult execute_plan(const Plan &plan, const ExecCtx &ctx);

} // namespace rankd
