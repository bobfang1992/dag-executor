#pragma once

#include <coroutine>
#include <cstddef>
#include <queue>

#include "event_loop.h"

namespace ranking {

/**
 * AsyncInflightLimiter - coroutine-friendly concurrency limiter with FIFO ordering.
 *
 * Unlike the synchronous InflightLimiter (which blocks threads with semaphores),
 * this limiter suspends coroutines and resumes them in FIFO order when permits
 * become available.
 *
 * Thread safety: This class is NOT thread-safe. All operations must be called
 * from the EventLoop thread. The design assumes coroutines using this limiter
 * are driven by the event loop, so no locks are needed.
 *
 * Usage:
 *   AsyncInflightLimiter limiter(64);  // max 64 concurrent ops
 *
 *   Task<void> doWork() {
 *     auto guard = co_await limiter.acquire();
 *     // ... do async Redis operation ...
 *     // guard destructor releases permit
 *   }
 */
class AsyncInflightLimiter {
 public:
  /**
   * RAII guard that releases a permit on destruction.
   *
   * IMPORTANT: Must be destroyed on the event loop thread.
   */
  class Guard {
   public:
    Guard() : limiter_(nullptr) {}
    explicit Guard(AsyncInflightLimiter* limiter) : limiter_(limiter) {}

    ~Guard() {
      if (limiter_) {
        limiter_->release();
      }
    }

    // Non-copyable
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

    // Movable
    Guard(Guard&& other) noexcept : limiter_(other.limiter_) {
      other.limiter_ = nullptr;
    }

    Guard& operator=(Guard&& other) noexcept {
      if (this != &other) {
        if (limiter_) {
          limiter_->release();
        }
        limiter_ = other.limiter_;
        other.limiter_ = nullptr;
      }
      return *this;
    }

    // Check if guard holds a permit
    explicit operator bool() const { return limiter_ != nullptr; }

   private:
    AsyncInflightLimiter* limiter_;
  };

  /**
   * Awaitable for acquiring a permit.
   *
   * If a permit is available, returns immediately.
   * Otherwise, suspends the coroutine and queues it for FIFO resumption.
   */
  class AcquireAwaitable {
   public:
    explicit AcquireAwaitable(AsyncInflightLimiter& limiter) : limiter_(limiter) {}

    // Ready if permit available
    bool await_ready() const noexcept { return limiter_.try_acquire(); }

    // Suspend and queue for resumption
    void await_suspend(std::coroutine_handle<> h) noexcept { limiter_.queue_waiter(h); }

    // Return RAII guard
    Guard await_resume() noexcept { return Guard(&limiter_); }

   private:
    AsyncInflightLimiter& limiter_;
  };

  /**
   * Create a limiter with the given maximum concurrent permits.
   *
   * @param max_permits Maximum number of concurrent operations (must be > 0)
   */
  explicit AsyncInflightLimiter(size_t max_permits) : max_permits_(max_permits), current_(0) {}

  // Non-copyable, non-movable (has waiters queue)
  AsyncInflightLimiter(const AsyncInflightLimiter&) = delete;
  AsyncInflightLimiter& operator=(const AsyncInflightLimiter&) = delete;
  AsyncInflightLimiter(AsyncInflightLimiter&&) = delete;
  AsyncInflightLimiter& operator=(AsyncInflightLimiter&&) = delete;

  /**
   * Acquire a permit asynchronously.
   *
   * co_await this to get a Guard that releases the permit on destruction.
   * If no permit is available, the coroutine suspends until one is released.
   * Waiters are resumed in FIFO order.
   */
  AcquireAwaitable acquire() { return AcquireAwaitable(*this); }

  /**
   * Try to acquire a permit synchronously (non-blocking).
   *
   * @return true if permit acquired, false if at limit
   */
  bool try_acquire() {
    if (current_ < max_permits_) {
      ++current_;
      return true;
    }
    return false;
  }

  /**
   * Release a permit.
   *
   * If waiters are queued, resumes the next one in FIFO order.
   * Called automatically by Guard destructor.
   */
  void release() {
    if (!waiters_.empty()) {
      // Grant permit directly to next waiter (no decrement/increment dance)
      auto h = waiters_.front();
      waiters_.pop();
      h.resume();
    } else {
      --current_;
    }
  }

  // Accessors for testing
  size_t max_permits() const { return max_permits_; }
  size_t current() const { return current_; }
  size_t waiters_count() const { return waiters_.size(); }

 private:
  void queue_waiter(std::coroutine_handle<> h) { waiters_.push(h); }

  size_t max_permits_;
  size_t current_;
  std::queue<std::coroutine_handle<>> waiters_;
};

}  // namespace ranking
