#pragma once

#include "param_table.h"
#include "plan.h"
#include "task_registry.h"
#include <vector>

namespace rankd {

// Validate plan (fail-closed). Throws std::runtime_error on failure.
void validate_plan(const Plan &plan);

// Execute plan and return output RowSets (one per outputs entry).
std::vector<RowSet> execute_plan(const Plan &plan, const ExecCtx &ctx);

} // namespace rankd
