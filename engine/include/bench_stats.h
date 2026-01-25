#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include <sys/resource.h>

namespace ranking {

// Latency statistics with percentiles
struct LatencyStats {
  double min_us = 0.0;
  double max_us = 0.0;
  double mean_us = 0.0;
  double p50_us = 0.0;
  double p90_us = 0.0;
  double p99_us = 0.0;
  size_t count = 0;
};

// Compute latency statistics from a vector of latencies in microseconds.
// IMPORTANT: Modifies the input vector (sorts in place for efficiency).
inline LatencyStats compute_latency_stats(std::vector<double>& latencies_us) {
  LatencyStats stats;
  if (latencies_us.empty()) {
    return stats;
  }

  stats.count = latencies_us.size();

  // Sort for percentile computation
  std::sort(latencies_us.begin(), latencies_us.end());

  stats.min_us = latencies_us.front();
  stats.max_us = latencies_us.back();

  double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
  stats.mean_us = sum / static_cast<double>(stats.count);

  // Percentile indices (0-based)
  auto percentile_idx = [&](double p) -> size_t {
    size_t idx = static_cast<size_t>(p * static_cast<double>(stats.count));
    if (idx >= stats.count) {
      idx = stats.count - 1;
    }
    return idx;
  };

  stats.p50_us = latencies_us[percentile_idx(0.50)];
  stats.p90_us = latencies_us[percentile_idx(0.90)];
  stats.p99_us = latencies_us[percentile_idx(0.99)];

  return stats;
}

// Get peak RSS in KB using getrusage.
// Note: macOS returns bytes, Linux returns KB. We normalize to KB.
inline int64_t get_peak_rss_kb() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return -1;
  }
#ifdef __APPLE__
  // macOS: ru_maxrss is in bytes
  return usage.ru_maxrss / 1024;
#else
  // Linux: ru_maxrss is in KB
  return usage.ru_maxrss;
#endif
}

// Get current RSS in KB.
// macOS: Uses mach task_info for accurate current RSS.
// Linux: Falls back to peak RSS (no easy way to get current without /proc).
inline int64_t get_current_rss_kb() {
#ifdef __APPLE__
  struct mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return get_peak_rss_kb();  // Fallback
  }
  return static_cast<int64_t>(info.resident_size) / 1024;
#else
  // Linux: Use peak RSS as approximation (reading /proc is slower)
  return get_peak_rss_kb();
#endif
}

}  // namespace ranking
