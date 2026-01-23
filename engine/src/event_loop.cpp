#include "event_loop.h"

#include <stdexcept>

namespace ranking {

EventLoop::EventLoop() {
  int r = uv_loop_init(&loop_);
  if (r != 0) {
    throw std::runtime_error("uv_loop_init failed: " + std::string(uv_strerror(r)));
  }
}

EventLoop::~EventLoop() {
  Stop();

  // Wait for the loop thread to fully exit before closing the loop
  // Skip if we're on the loop thread (destructor called from callback)
  bool on_loop_thread = started_.load() &&
                        (std::this_thread::get_id() == loop_thread_id_);
  if (started_.load() && !on_loop_thread) {
    std::unique_lock<std::mutex> lock(exit_mutex_);
    exit_cv_.wait(lock, [this]() { return exited_; });
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

  running_.store(true);

  loop_thread_ = std::thread([this]() {
    // Run the loop until Stop() is called
    uv_run(&loop_, UV_RUN_DEFAULT);

    // Signal that the loop has exited (unless detached - object may be destroyed)
    if (!detached_.load()) {
      std::lock_guard<std::mutex> lock(exit_mutex_);
      exited_ = true;
      exit_cv_.notify_all();
    }
  });
  loop_thread_id_ = loop_thread_.get_id();
}

void EventLoop::Stop() {
  // Use stopping_ to prevent re-entry and signal Post() to reject new callbacks
  bool expected = false;
  if (!stopping_.compare_exchange_strong(expected, true)) {
    return;  // Already stopping
  }

  if (!started_.load()) {
    return;  // Never started
  }

  // Check if we're on the loop thread
  bool on_loop_thread = (std::this_thread::get_id() == loop_thread_id_);

  if (on_loop_thread) {
    // Stop inline - don't queue a lambda that captures `this`
    // This avoids use-after-free if EventLoop is destroyed from a callback
    running_.store(false);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
    uv_stop(&loop_);

    // Signal exit for any waiters (though destructor will skip wait on loop thread)
    {
      std::lock_guard<std::mutex> lock(exit_mutex_);
      exited_ = true;
    }
    exit_cv_.notify_all();

    // Detach to avoid std::terminate on destruction
    // Set detached_ so thread skips cleanup after uv_run (object may be destroyed)
    if (loop_thread_.joinable()) {
      detached_.store(true);
      loop_thread_.detach();
    }
  } else {
    // Queue shutdown and wait for completion
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      queue_.push([this]() {
        running_.store(false);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
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
  // Reject posts before Start() or after Stop() begins
  if (!started_.load() || stopping_.load()) {
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
