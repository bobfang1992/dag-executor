#pragma once

#include "rowset.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rankd {

// Forward declarations
struct ValidatedParams;

// =============================================================================
// OutputPattern: Defines the expected shape/contract of a task's output
// =============================================================================
//
// Each task declares ONE output pattern in its TaskSpec. The executor validates
// the task's output against this pattern immediately after execution. This is
// the SINGLE place where output-shape rules are defined and enforced.
//
// Patterns and their semantics:
//
// 1) SourceFanoutDense
//    - For source tasks that create N new rows (e.g., viewer.follow)
//    - Output rowCount() must equal params["fanout"]
//    - Active rows must be dense [0..N) in natural order
//
// 2) UnaryPreserveView
//    - For transform tasks that don't change which rows are active (e.g., vm)
//    - Output rowCount() must equal input[0].rowCount()
//    - Output activeRows() sequence must exactly equal input[0].activeRows()
//
// 3) StableFilter
//    - For filter tasks that select a subset of active rows (e.g., filter)
//    - Output rowCount() must equal input[0].rowCount()
//    - Output activeRows() must be a subsequence of input[0].activeRows()
//
// 4) PrefixOfInput
//    - For take-like tasks that truncate to first K active rows (e.g., take)
//    - Output rowCount() must equal input[0].rowCount()
//    - Output activeRows() must be first K of input[0].activeRows()
//      where K = min(params["count"], input[0].logicalSize())
//
// 5) ConcatDense
//    - For concat tasks that merge two inputs into a new dense batch
//    - Must have exactly 2 inputs
//    - Output rowCount() must equal |lhs.active| + |rhs.active|
//    - Active rows must be dense [0..N) in natural order
//
// =============================================================================

enum class OutputPattern {
  SourceFanoutDense,  // sources that create N rows (fanout)
  UnaryPreserveView,  // vm: same physical rowCount, same active order
  StableFilter,       // filter: output active is subsequence of input active
  PrefixOfInput,      // take: output active is prefix of input active (count)
  ConcatDense         // concat: out rowCount = |lhs.active| + |rhs.active|
};

// Convert OutputPattern to string for error messages
const char *outputPatternToString(OutputPattern pattern);

// =============================================================================
// ValidateTaskOutput: Centralized output validation
// =============================================================================
//
// Called by executor immediately after each task runs.
// Throws std::runtime_error with deterministic error message on violation.
//
// Error format:
//   "Error: Node 'NODE_ID': op 'OP' violated output contract: DETAILS"
//
// Parameters:
//   node_id   - Plan node ID (for error messages)
//   op        - Task operation name (for error messages)
//   pattern   - Expected output pattern from TaskSpec
//   inputs    - Task inputs (for validation against input shapes)
//   params    - Validated parameters (for extracting fanout, count, etc.)
//   output    - Task output to validate
//
void validateTaskOutput(const std::string &node_id, const std::string &op,
                        OutputPattern pattern,
                        const std::vector<RowSet> &inputs,
                        const ValidatedParams &params, const RowSet &output);

} // namespace rankd
