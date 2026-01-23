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

  // Timer handles are SleepState - use proper close callback to free memory
  if (handle->type == UV_TIMER) {
    uv_timer_stop(reinterpret_cast<uv_timer_t*>(handle));
    uv_close(handle, SleepState::OnClose);
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
  Stop();

  // Callbacks must NOT destroy the EventLoop. If we're on the loop thread,
  // uv_run() is still on the stack and destroying loop_ would cause UAF.
  // This is a programming error - assert to catch it during development.
  bool on_loop_thread = started_.load() &&
                        (std::this_thread::get_id() == loop_thread_id_);
  assert(!on_loop_thread && "EventLoop destroyed from its own callback - this is undefined behavior");

  // Wait for the loop thread to fully exit before closing the loop
  if (started_.load()) {
    std::unique_lock<std::mutex> lock(exit_state_->exit_mutex);
    exit_state_->exit_cv.wait(lock, [this]() { return exit_state_->exited; });
  }

  uv_loop_close(&loop_);
}

void EventLoop::Start() {
  bool expected = false;
  if (!started_.compare_exchange_strong(expected, true)) {
    return;  // Already started
  }

  // Initialize the async handle for cross-thread signaling
  int r = uv_async_init(&loop_, &async_, OnAsync);
  if (r != 0) {
    started_.store(false);
    throw std::runtime_error("uv_async_init failed: " + std::string(uv_strerror(r)));
  }
  async_.data = this;

  // Check if Stop() was called during initialization.
  // If so, clean up and don't start the loop thread.
  if (stopping_.load()) {
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
    uv_run(&loop_, UV_RUN_NOWAIT);  // Process the close
    started_.store(false);
    return;
  }

  running_.store(true);

  // Capture exit_state_ by value (shared_ptr copy) so thread can safely access
  // it even after EventLoop is destroyed (e.g., Stop called from callback)
  auto exit_state = exit_state_;
  loop_thread_ = std::thread([this, exit_state]() {
    // Run the loop until Stop() is called
    uv_run(&loop_, UV_RUN_DEFAULT);

    // Always signal exit after uv_run returns. This is safe even if EventLoop
    // is destroyed because exit_state is a shared_ptr copy. Destructor waits
    // on this signal before calling uv_loop_close.
    std::lock_guard<std::mutex> lock(exit_state->exit_mutex);
    exit_state->exited = true;
    exit_state->exit_cv.notify_all();
  });
  loop_thread_id_ = loop_thread_.get_id();
}

void EventLoop::Stop() {
  // Use stopping_ to prevent re-entry and signal Post() to reject new callbacks
  bool expected = false;
  if (!stopping_.compare_exchange_strong(expected, true)) {
    return;  // Already stopping
  }

  // Check running_ (not started_) because started_ is set before uv_async_init
  // completes. If we only checked started_, we could call uv_async_send on
  // an uninitialized handle.
  // Keep stopping_=true so Start() can detect the pending stop request.
  if (!running_.load()) {
    return;  // Never started or async handle not ready - Start() will check stopping_
  }

  // Check if we're on the loop thread
  bool on_loop_thread = (std::this_thread::get_id() == loop_thread_id_);

  if (on_loop_thread) {
    // Stop inline - don't queue a lambda that captures `this`
    // This avoids use-after-free if EventLoop is destroyed from a callback
    running_.store(false);

    // Drain any callbacks that were accepted before stopping_ was set.
    // Without this, callbacks posted while the loop is busy can be dropped
    // if Stop() is invoked on the loop thread.
    DrainQueue();

    // Close all pending handles (timers, etc.) to prevent leaks
    uv_walk(&loop_, CloseWalkCallback, &async_);

    // Close the async handle last
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);

    // Tell libuv to exit after processing close callbacks.
    // Don't call uv_run here - we're already inside uv_run and re-entry is UB.
    // The outer uv_run will process close callbacks before exiting.
    uv_stop(&loop_);

    // Do NOT signal exited here - we're still inside uv_run() on the stack.
    // The loop thread lambda will signal after uv_run() actually returns.
    // Destructor waits on exit_cv regardless of detach status.

    // Detach to avoid std::terminate on destruction
    if (loop_thread_.joinable()) {
      loop_thread_.detach();
    }
  } else {
    // Queue shutdown and wait for completion
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      queue_.push([this]() {
        running_.store(false);

        // Close all pending handles (timers, etc.) to prevent leaks
        uv_walk(&loop_, CloseWalkCallback, &async_);

        // Close the async handle last
        uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);

        // Tell libuv to exit after processing close callbacks.
        // Don't call uv_run here - we're inside uv_run and re-entry is UB.
        uv_stop(&loop_);
      });
    }
    uv_async_send(&async_);

    if (loop_thread_.joinable()) {
      loop_thread_.join();
    }
  }
}

bool EventLoop::Post(std::function<void()> fn) {
  // Reject posts before Start() completes or after Stop() begins.
  // Use running_ (not started_) because started_ is set before uv_async_init
  // completes, creating a race where Post could send to uninitialized handle.
  if (!running_.load() || stopping_.load()) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push(std::move(fn));
  }
  // Wake up the loop thread
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
