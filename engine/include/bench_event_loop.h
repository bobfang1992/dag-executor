#pragma once

#include <string>

namespace ranking {

// Configuration for EventLoop benchmarks
struct BenchEventLoopConfig {
  std::string mode = "all";      // posts|timers|sleep_vs_pool|all
  int n = 0;                     // 0 = use mode default
  int producers = 1;             // for posts mode
  int sleep_ms = 1;              // for timer/sleep modes
  int tasks = 1000;              // for timer/sleep modes
  bool json_output = false;      // JSON vs human-readable output
};

// Run EventLoop benchmarks according to config.
// Returns 0 on success, non-zero on error.
int run_bench_eventloop(const BenchEventLoopConfig& config);

}  // namespace ranking
