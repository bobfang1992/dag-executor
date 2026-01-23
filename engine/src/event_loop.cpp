#include "event_loop.h"

#include <cassert>
#include <stdexcept>

#include "uv_sleep.h"

namespace ranking {

namespace {
// Callback to close all handles during shutdown.
// For timer handles (SleepState), we use the proper close callback to free memory.
void CloseWalkCallback(uv_handle_t* handle, void* arg) {
  auto* async_handle = static_cast<uv_async_t*>(arg);
  // Don't close the async handle here - we handle it separately
  if (handle == reinterpret_cast<uv_handle_t*>(async_handle) || uv_is_closing(handle)) {
    return;
  }

  // Timer handles MAY be SleepState - check if tagged (data == handle address)
  // Only delete tagged timers; others may be stack/member allocated
  if (handle->type == UV_TIMER) {
    uv_timer_stop(reinterpret_cast<uv_timer_t*>(handle));
    if (handle->data == handle) {
      // Tagged SleepState timer - use proper close callback to free memory
      uv_close(handle, SleepState::OnClose);
    } else {
      // External timer - just close, don't delete
      uv_close(handle, nullptr);
    }
  } else {
    uv_close(handle, nullptr);
  }
}
}  // namespace

EventLoop::EventLoop() : exit_state_(std::make_shared<EventLoopExitState>()) {
  int r = uv_loop_init(&loop_);
  if (r != 0) {
    throw std::runtime_error("uv_loop_init failed: " + std::string(uv_strerror(r)));
  }
}

EventLoop::~EventLoop() {
  // Destructor on loop thread = deadlock (would wait on exit_cv forever).
  // This is a programming error - callbacks must not own the EventLoop.
  assert(loop_thread_.get_id() != std::this_thread::get_id() &&
         "EventLoop destroyed from its own callback - undefined behavior");

  Stop();

  // Wait for the loop thread to fully exit before closing the loop.
  // The thread signals exit_state_->exited after uv_run returns.
  State s = state_.load();
  if (s == State::Stopped || s == State::Stopping) {
    std::unique_lock<std::mutex> lock(exit_state_->exit_mutex);
    exit_state_->exit_cv.wait(lock, [this]() { return exit_state_->exited; });
  }

  // Join thread if Stop() couldn't (e.g., Stop raced with Start before thread created)
  if (loop_thread_.joinable()) {
    loop_thread_.join();
  }

  // Always close the loop - uv_loop_init is called in constructor
  uv_loop_close(&loop_);
}

void EventLoop::Start() {
  // Transition: Idle → Starting (only valid from Idle)
  State expected = State::Idle;
  if (!state_.compare_exchange_strong(expected, State::Starting)) {
    return;  // Already started or starting
  }

  // Initialize the async handle for cross-thread signaling
  int r = uv_async_init(&loop_, &async_, OnAsync);
  if (r != 0) {
    state_.store(State::Idle);
    throw std::runtime_error("uv_async_init failed: " + std::string(uv_strerror(r)));
  }
  async_.data = this;

  // Capture exit_state_ by value (shared_ptr copy) so thread can safely access
  // it even after EventLoop is destroyed
  auto exit_state = exit_state_;
  try {
    loop_thread_ = std::thread([this, exit_state]() {
      // Set own thread ID first, before any callbacks can run
      loop_thread_id_ = std::this_thread::get_id();

      // Transition: Starting → Running (we're now ready)
      State expected = State::Starting;
      if (!state_.compare_exchange_strong(expected, State::Running)) {
        // Stop was called during init - go directly to shutdown
        DoStop();
        // MUST still signal exit so destructor doesn't deadlock
        std::lock_guard<std::mutex> lock(exit_state->exit_mutex);
        exit_state->exited = true;
        exit_state->exit_cv.notify_all();
        return;
      }

      // Run the loop until Stop() is called
      uv_run(&loop_, UV_RUN_DEFAULT);

      // Signal exit - safe even if EventLoop is destroyed (shared_ptr)
      std::lock_guard<std::mutex> lock(exit_state->exit_mutex);
      exit_state->exited = true;
      exit_state->exit_cv.notify_all();
    });
  } catch (...) {
    // Thread creation failed - clean up
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
    uv_run(&loop_, UV_RUN_NOWAIT);
    state_.store(State::Idle);
    throw;
  }

  // Wait for loop thread to reach Running state (or shutdown path)
  // This ensures Post() will work immediately after Start() returns
  while (true) {
    State s = state_.load();
    if (s == State::Running || s == State::Stopping || s == State::Stopped) {
      break;
    }
    std::this_thread::yield();
  }
}

void EventLoop::Stop() {
  // Try to transition to Stopping from valid states
  while (true) {
    State s = state_.load();

    switch (s) {
      case State::Idle:
        // Never started - nothing to do
        return;

      case State::Starting:
        // Try to abort startup: Starting → Stopping
        if (state_.compare_exchange_strong(s, State::Stopping)) {
          // The thread will see Stopping and call DoStop()
          // Wait for thread to finish
          if (loop_thread_.joinable()) {
            loop_thread_.join();
          }
          return;
        }
        // CAS failed - state changed, retry
        continue;

      case State::Running: {
        // Normal case: Running → Stopping
        if (!state_.compare_exchange_strong(s, State::Stopping)) {
          continue;  // CAS failed, retry
        }

        // Check if we're on the loop thread
        bool on_loop_thread = (std::this_thread::get_id() == loop_thread_id_);

        if (on_loop_thread) {
          // Stop inline
          DoStop();
          if (loop_thread_.joinable()) {
            loop_thread_.detach();
          }
        } else {
          // Queue shutdown to loop thread
          {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push([this]() { DoStop(); });
          }
          uv_async_send(&async_);

          if (loop_thread_.joinable()) {
            loop_thread_.join();
          }
        }
        return;
      }

      case State::Stopping:
      case State::Stopped:
        // Already stopping or stopped
        return;
    }
  }
}

void EventLoop::DoStop() {
  // Drain any pending callbacks
  DrainQueue();

  // Close all pending handles
  uv_walk(&loop_, CloseWalkCallback, &async_);

  // Close the async handle last
  uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);

  // Tell libuv to exit
  uv_stop(&loop_);

  // Transition to Stopped
  state_.store(State::Stopped);
}

bool EventLoop::Post(std::function<void()> fn) {
  // Only accept posts in Running state
  if (state_.load() != State::Running) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // Re-check under lock to prevent race with Stop() draining queue
    if (state_.load() != State::Running) {
      return false;
    }
    queue_.push(std::move(fn));
  }
  uv_async_send(&async_);
  return true;
}

void EventLoop::OnAsync(uv_async_t* handle) {
  auto* self = static_cast<EventLoop*>(handle->data);
  self->DrainQueue();
}

void EventLoop::DrainQueue() {
  std::queue<std::function<void()>> local_queue;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::swap(local_queue, queue_);
  }
  while (!local_queue.empty()) {
    local_queue.front()();
    local_queue.pop();
  }
}

}  // namespace ranking
