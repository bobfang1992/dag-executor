#include "thread_pool.h"

namespace rankd {

ThreadPool::ThreadPool(size_t num_threads) {
  workers_.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
          if (stop_ && tasks_.empty()) {
            return;
          }
          task = std::move(tasks_.front());
          tasks_.pop();
        }
        task();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

// Global IO thread pool - lazily initialized
ThreadPool &GetIOThreadPool() {
  static ThreadPool pool(4); // 4 threads for IO operations
  return pool;
}

} // namespace rankd
