#pragma once

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

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::forward<F>(f));
    std::future<ReturnType> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) {
        throw std::runtime_error("submit on stopped ThreadPool");
      }
      tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return result;
  }

  // Get number of worker threads
  size_t size() const { return workers_.size(); }

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};

// Global thread pool singleton for IO operations
ThreadPool &GetIOThreadPool();

} // namespace rankd
