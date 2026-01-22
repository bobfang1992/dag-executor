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
  });
}

void EventLoop::Stop() {
  if (!running_.exchange(false)) {
    return;  // Already stopped or never started
  }

  // Post a callback that stops the loop
  Post([this]() {
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
    uv_stop(&loop_);
  });

  if (loop_thread_.joinable()) {
    loop_thread_.join();
  }
}

void EventLoop::Post(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push(std::move(fn));
  }
  // Wake up the loop thread
  uv_async_send(&async_);
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
