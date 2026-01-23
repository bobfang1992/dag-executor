#pragma once

#include <uv.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace ranking {

// Shared state for loop thread synchronization.
// Held by both EventLoop and the loop thread via shared_ptr, so the thread
// can safely signal exit even after EventLoop is destroyed.
struct EventLoopExitState {
  std::mutex exit_mutex;
  std::condition_variable exit_cv;
  bool exited{false};
};

// Single-threaded libuv event loop wrapper.
// Provides thread-safe posting of callbacks to be executed on the loop thread.
//
// Usage pattern:
//   - Scheduler owns EventLoop lifecycle (Start/Stop called from one thread)
//   - Worker threads call Post() to schedule async work (thread-safe)
//   - Event loop thread executes callbacks and IO polling
//
// The state machine handles concurrent Start/Stop for robustness, but in
// practice the scheduler controls lifecycle sequentially. Consider singleton
// pattern if Start/Stop complexity becomes unnecessary.
//
// Lifecycle state machine:
//   Idle → Starting → Running → Stopping → Stopped
//                 ↘ Stopped (if Stop called during init)
//
// All transitions are atomic CAS operations - no race windows.
class EventLoop {
public:
  // Lifecycle states
  enum class State : int {
    Idle,      // Not started
    Starting,  // Init in progress (uv_async_init, thread creation)
    Running,   // Loop thread active, accepting Post()
    Stopping,  // Shutdown in progress
    Stopped    // Done, can be destroyed
  };

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
  bool IsRunning() const { return state_.load() == State::Running; }

  // Get current state (for testing/debugging)
  State GetState() const { return state_.load(); }

private:
  static void OnAsync(uv_async_t* handle);
  void DrainQueue();
  void DoStop();  // Internal stop logic, called on loop thread

  uv_loop_t loop_;
  uv_async_t async_;
  std::thread loop_thread_;
  std::thread::id loop_thread_id_;
  std::mutex queue_mutex_;
  std::queue<std::function<void()>> queue_;

  // Single atomic state - eliminates all race conditions between flags
  std::atomic<State> state_{State::Idle};

  // Shared exit state - survives EventLoop destruction for safe thread cleanup
  std::shared_ptr<EventLoopExitState> exit_state_;
};

}  // namespace ranking
