#pragma once

#include <cstddef>

#include "thread_pool.h"

namespace rankd {

// Initialize the CPU thread pool with the given number of threads.
// Must be called before GetCPUThreadPool() (typically from main()).
// Default: 8 threads.
void InitCPUThreadPool(size_t num_threads = 8);

// Get the global CPU thread pool for DAG node execution.
// Throws if InitCPUThreadPool() has not been called.
ThreadPool& GetCPUThreadPool();

}  // namespace rankd
