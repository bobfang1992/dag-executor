#pragma once

#include <chrono>
#include <optional>

namespace ranking {

using SteadyTimePoint = std::chrono::steady_clock::time_point;
using OptionalDeadline = std::optional<SteadyTimePoint>;

/**
 * Check if a deadline has been exceeded.
 *
 * @param deadline Optional deadline time point
 * @return true if deadline is set and current time >= deadline
 */
inline bool deadline_exceeded(OptionalDeadline deadline) {
  return deadline && std::chrono::steady_clock::now() >= *deadline;
}

/**
 * Check if a deadline has been exceeded at a given time.
 *
 * @param now The current time (for deterministic testing)
 * @param deadline Optional deadline time point
 * @return true if deadline is set and now >= deadline
 */
inline bool deadline_exceeded_at(SteadyTimePoint now, OptionalDeadline deadline) {
  return deadline && now >= *deadline;
}

/**
 * Compute the effective deadline for a node, taking the earlier of:
 * - request deadline (global deadline for entire request)
 * - start_time + node_timeout (per-node timeout)
 *
 * If neither is set, returns nullopt (no deadline).
 *
 * @param start_time Node execution start time (captured once for determinism)
 * @param request_deadline Request-level deadline
 * @param node_timeout Per-node timeout duration
 * @return Effective deadline (earliest of the two constraints)
 */
inline OptionalDeadline compute_effective_deadline(
    SteadyTimePoint start_time,
    OptionalDeadline request_deadline,
    std::optional<std::chrono::milliseconds> node_timeout) {
  OptionalDeadline effective;

  if (request_deadline) {
    effective = *request_deadline;
  }

  if (node_timeout) {
    auto node_deadline = start_time + *node_timeout;
    if (!effective || node_deadline < *effective) {
      effective = node_deadline;
    }
  }

  return effective;
}

/**
 * Compute milliseconds remaining until deadline.
 *
 * @param now Current time
 * @param deadline Deadline time point
 * @return Milliseconds remaining (0 if already exceeded)
 */
inline uint64_t ms_until_deadline(SteadyTimePoint now, SteadyTimePoint deadline) {
  if (now >= deadline) {
    return 0;
  }
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

}  // namespace ranking
