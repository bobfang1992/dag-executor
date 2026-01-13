#pragma once

#include "plan.h"
#include "task_registry.h"
#include <vector>

namespace rankd {

// Validate plan (fail-closed). Throws std::runtime_error on failure.
void validate_plan(const Plan& plan);

// Execute plan and return output RowSets (one per outputs entry).
std::vector<RowSet> execute_plan(const Plan& plan);

} // namespace rankd
