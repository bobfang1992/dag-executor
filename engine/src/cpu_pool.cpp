#include "cpu_pool.h"

#include <memory>
#include <stdexcept>

namespace rankd {

namespace {
std::unique_ptr<ThreadPool> g_cpu_pool;
}  // namespace

void InitCPUThreadPool(size_t num_threads) {
  if (g_cpu_pool) {
    throw std::runtime_error("CPU thread pool already initialized");
  }
  g_cpu_pool = std::make_unique<ThreadPool>(num_threads);
}

ThreadPool& GetCPUThreadPool() {
  if (!g_cpu_pool) {
    throw std::runtime_error("CPU thread pool not initialized. Call InitCPUThreadPool() first.");
  }
  return *g_cpu_pool;
}

}  // namespace rankd
