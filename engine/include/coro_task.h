#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace ranking {

// Forward declaration
template <typename T>
struct Task;

namespace detail {

// Base promise type with common functionality
struct PromiseBase {
  std::exception_ptr exception_;
  std::coroutine_handle<> continuation_;

  std::suspend_always initial_suspend() noexcept { return {}; }

  struct FinalAwaiter {
    bool await_ready() noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
      auto& promise = h.promise();
      if (promise.continuation_) {
        return promise.continuation_;
      }
      return std::noop_coroutine();
    }

    void await_resume() noexcept {}
  };

  FinalAwaiter final_suspend() noexcept { return {}; }

  void unhandled_exception() { exception_ = std::current_exception(); }
};

}  // namespace detail

// Task<T> - lazy coroutine that returns a value
template <typename T>
struct Task {
  struct promise_type : detail::PromiseBase {
    std::optional<T> value_;

    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    void return_value(T value) { value_ = std::move(value); }
  };

  using handle_type = std::coroutine_handle<promise_type>;

  explicit Task(handle_type h) : handle_(h) {}

  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  // Non-copyable
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  // Awaitable interface
  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
    handle_.promise().continuation_ = awaiting;
    return handle_;
  }

  T await_resume() {
    if (handle_.promise().exception_) {
      std::rethrow_exception(handle_.promise().exception_);
    }
    return std::move(*handle_.promise().value_);
  }

  // Start the coroutine (for use with blockingWait)
  void start() {
    if (handle_ && !handle_.done()) {
      handle_.resume();
    }
  }

  // Check if done
  bool done() const { return handle_.done(); }

  // Get result (for blockingWait)
  T result() {
    if (handle_.promise().exception_) {
      std::rethrow_exception(handle_.promise().exception_);
    }
    return std::move(*handle_.promise().value_);
  }

  handle_type handle_;
};

// Task<void> specialization
template <>
struct Task<void> {
  struct promise_type : detail::PromiseBase {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    void return_void() {}
  };

  using handle_type = std::coroutine_handle<promise_type>;

  explicit Task(handle_type h) : handle_(h) {}

  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  // Non-copyable
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  // Awaitable interface
  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
    handle_.promise().continuation_ = awaiting;
    return handle_;
  }

  void await_resume() {
    if (handle_.promise().exception_) {
      std::rethrow_exception(handle_.promise().exception_);
    }
  }

  // Start the coroutine (for use with blockingWait)
  void start() {
    if (handle_ && !handle_.done()) {
      handle_.resume();
    }
  }

  // Check if done
  bool done() const { return handle_.done(); }

  // Get result (throws if exception)
  void result() {
    if (handle_.promise().exception_) {
      std::rethrow_exception(handle_.promise().exception_);
    }
  }

  handle_type handle_;
};

}  // namespace ranking
