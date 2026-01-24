#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace rankd {

// Simple thread pool for offloading blocking IO operations.
// Used to run Redis calls with inflight limiting without blocking the main thread.
class ThreadPool {
public:
  explicit ThreadPool(size_t num_threads = 4);
  ~ThreadPool();

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  // Submit a task and get a future for the result
  template <typename F>
  auto submit(F &&f) -> std::future<std::invoke_result_t<F>> {
    using ReturnType = std::invoke_result_t<F>;

    // Wrap task to track in-flight count
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::forward<F>(f));
    std::future<ReturnType> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) {
        throw std::runtime_error("submit on stopped ThreadPool");
      }
      ++in_flight_;
      tasks_.emplace([this, task]() {
        (*task)();
        // Decrement and notify waiters
        if (--in_flight_ == 0) {
          idle_cv_.notify_all();
        }
      });
    }
    cv_.notify_one();
    return result;
  }

  // Wait for all in-flight tasks to complete (drain).
  // Call this before destroying resources that tasks may reference.
  void wait_idle();

  // Get number of worker threads
  size_t size() const { return workers_.size(); }

  // Get number of in-flight tasks
  size_t in_flight() const { return in_flight_.load(); }

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable idle_cv_;
  std::atomic<size_t> in_flight_{0};
  bool stop_ = false;
};

// Global thread pool singleton for IO operations
ThreadPool &GetIOThreadPool();

} // namespace rankd
