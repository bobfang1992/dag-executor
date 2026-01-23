#pragma once

#include <uv.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace ranking {

// Single-threaded libuv event loop wrapper.
// Provides thread-safe posting of callbacks to be executed on the loop thread.
class EventLoop {
public:
  EventLoop();
  ~EventLoop();

  // Non-copyable, non-movable
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;

  // Start the event loop thread. Idempotent.
  void Start();

  // Stop the event loop and join the thread. Idempotent.
  void Stop();

  // Post a callback to be executed on the loop thread.
  // Thread-safe; can be called from any thread.
  // Returns false if the loop is not running (not started or stopping).
  bool Post(std::function<void()> fn);

  // Access the raw libuv loop handle.
  // Only valid after Start() and before Stop().
  uv_loop_t* RawLoop() { return &loop_; }

  // Check if the loop is running
  bool IsRunning() const { return running_.load(); }

private:
  static void OnAsync(uv_async_t* handle);
  void DrainQueue();

  uv_loop_t loop_;
  uv_async_t async_;
  std::thread loop_thread_;
  std::thread::id loop_thread_id_;
  std::mutex queue_mutex_;
  std::queue<std::function<void()>> queue_;
  std::atomic<bool> running_{false};
  std::atomic<bool> started_{false};
  std::atomic<bool> stopping_{false};

  // Shutdown synchronization - allows destructor to wait for loop exit
  std::mutex exit_mutex_;
  std::condition_variable exit_cv_;
  bool exited_{false};
};

}  // namespace ranking
