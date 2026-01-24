#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "cpu_pool.h"
#include "event_loop.h"

namespace ranking {

/**
 * OffloadCpu - awaitable that runs a callable on the CPU thread pool,
 * then resumes the coroutine on the EventLoop thread.
 *
 * This allows CPU-bound work (vm, filter, sort) to run on worker threads
 * while keeping the main scheduler on the single libuv event loop thread.
 *
 * Usage:
 *   Task<RowSet> myTask(EventLoop& loop) {
 *     RowSet result = co_await OffloadCpu(loop, [&]() {
 *       // CPU-intensive work here
 *       return computeRowSet();
 *     });
 *     co_return result;
 *   }
 *
 * Thread model:
 *   1. Coroutine suspends on event loop thread
 *   2. Callable runs on CPU pool thread
 *   3. Coroutine resumes on event loop thread (via Post)
 *
 * Error handling:
 *   Exceptions thrown by the callable are captured and rethrown on await_resume.
 */
template <typename F>
class OffloadCpu {
 public:
  using ResultType = std::invoke_result_t<F>;

  OffloadCpu(EventLoop& loop, F&& fn)
      : loop_(loop), fn_(std::forward<F>(fn)), result_(std::exception_ptr{}) {}

  // Never ready - always offload to CPU pool
  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) {
    // Submit work to CPU pool
    rankd::GetCPUThreadPool().submit([this, h]() {
      // Run the callable on CPU pool thread
      try {
        if constexpr (std::is_void_v<ResultType>) {
          fn_();
          result_.template emplace<0>(std::monostate{});
        } else {
          result_.template emplace<0>(fn_());
        }
      } catch (...) {
        result_.template emplace<1>(std::current_exception());
      }

      // Post resumption back to event loop thread
      // If Post fails (loop stopping), the coroutine won't resume.
      // This is acceptable during shutdown - the coroutine frame will be
      // destroyed when the owning Task is destroyed.
      loop_.Post([h]() { h.resume(); });
    });
  }

  ResultType await_resume() {
    // Check for exception
    if (std::holds_alternative<std::exception_ptr>(result_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(result_));
    }

    // Return result
    if constexpr (std::is_void_v<ResultType>) {
      return;
    } else {
      return std::move(std::get<0>(result_));
    }
  }

 private:
  EventLoop& loop_;
  F fn_;

  // Result storage: either the result value or an exception
  // For void functions, we use std::monostate as placeholder
  using StoredResult =
      std::conditional_t<std::is_void_v<ResultType>, std::monostate, ResultType>;
  std::variant<StoredResult, std::exception_ptr> result_;
};

// Deduction guide for OffloadCpu
template <typename F>
OffloadCpu(EventLoop&, F&&) -> OffloadCpu<std::decay_t<F>>;

/**
 * OffloadCpuWithTimeout - awaitable that runs a callable on the CPU thread pool
 * with deadline/timeout support.
 *
 * Key invariant: ALL state mutations happen on the loop thread only.
 * The CPU job Posts back to the loop thread before touching shared state.
 *
 * Thread model:
 *   1. Coroutine suspends on event loop thread
 *   2. CPU work submitted to thread pool
 *   3. Timer started on loop thread (if deadline set)
 *   4. First-wins: CPU completion or timeout
 *   5. Coroutine resumes on event loop thread
 *
 * If timeout fires before CPU completes:
 *   - Sets timeout error in result
 *   - Resumes coroutine with error
 *   - CPU job completes later (result discarded)
 *
 * If CPU completes before timeout:
 *   - Cancels timer (if set)
 *   - Resumes coroutine with result
 *
 * Usage:
 *   OptionalDeadline deadline = std::chrono::steady_clock::now() + 50ms;
 *   auto result = co_await OffloadCpuWithTimeout(loop, deadline, [&]() {
 *     return expensiveComputation();
 *   });
 */
template <typename F>
class OffloadCpuWithTimeout {
 public:
  using ResultType = std::invoke_result_t<F>;
  using StoredResult =
      std::conditional_t<std::is_void_v<ResultType>, std::monostate, ResultType>;

  // Shared state between CPU job, timer, and coroutine
  // Lives on heap so CPU job can safely access after coroutine resumes
  struct State {
    bool completed = false;  // First-wins guard (loop-thread only)
    std::variant<StoredResult, std::exception_ptr> result{std::exception_ptr{}};
    std::coroutine_handle<> handle;
    uv_timer_t* timer = nullptr;
    EventLoop* loop = nullptr;
  };

  OffloadCpuWithTimeout(EventLoop& loop, std::optional<std::chrono::steady_clock::time_point> deadline, F&& fn)
      : loop_(loop),
        deadline_(deadline),
        fn_(std::forward<F>(fn)),
        state_(std::make_shared<State>()) {
    state_->loop = &loop;
  }

  // Check if deadline already exceeded (don't suspend)
  bool await_ready() const noexcept {
    if (deadline_ && std::chrono::steady_clock::now() >= *deadline_) {
      // Deadline already exceeded - will set error in await_resume
      return true;
    }
    return false;
  }

  bool await_suspend(std::coroutine_handle<> h) {
    state_->handle = h;

    // Check deadline again (might have passed since await_ready)
    if (deadline_ && std::chrono::steady_clock::now() >= *deadline_) {
      state_->completed = true;
      state_->result = std::make_exception_ptr(
          std::runtime_error("Node execution timeout (deadline exceeded before start)"));
      return false;  // Don't suspend - resume immediately
    }

    // Submit CPU work
    auto state = state_;  // Capture shared_ptr for lambda
    auto fn = std::move(fn_);

    rankd::GetCPUThreadPool().submit([state, fn = std::move(fn)]() mutable {
      // Execute on CPU thread
      std::variant<StoredResult, std::exception_ptr> local_result{std::exception_ptr{}};
      try {
        if constexpr (std::is_void_v<ResultType>) {
          fn();
          local_result.template emplace<0>(std::monostate{});
        } else {
          local_result.template emplace<0>(fn());
        }
      } catch (...) {
        local_result.template emplace<1>(std::current_exception());
      }

      // Post back to loop thread before touching state
      state->loop->Post([state, local_result = std::move(local_result)]() mutable {
        if (state->completed) {
          return;  // Timer already fired
        }
        state->completed = true;
        state->result = std::move(local_result);

        // Cancel timer if active
        if (state->timer) {
          uv_timer_stop(state->timer);
          uv_close(reinterpret_cast<uv_handle_t*>(state->timer),
                   [](uv_handle_t* h) { delete reinterpret_cast<uv_timer_t*>(h); });
          state->timer = nullptr;
        }

        state->handle.resume();
      });
    });

    // Start deadline timer (if deadline set)
    if (deadline_) {
      auto now = std::chrono::steady_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline_ - now).count();
      if (ms <= 0) {
        ms = 1;  // Minimum 1ms to ensure timer fires
      }

      // Create and start timer on loop thread
      state = state_;  // Refresh capture
      loop_.Post([state, ms]() {
        if (state->completed) {
          return;  // CPU job already finished
        }

        auto* timer = new uv_timer_t;
        timer->data = state.get();
        state->timer = timer;

        uv_timer_init(state->loop->RawLoop(), timer);
        uv_timer_start(timer, OnTimeout, static_cast<uint64_t>(ms), 0);
      });
    }

    return true;  // Suspend
  }

  ResultType await_resume() {
    // Handle deadline-exceeded-before-start case
    if (!state_->completed && deadline_ && std::chrono::steady_clock::now() >= *deadline_) {
      throw std::runtime_error("Node execution timeout (deadline exceeded)");
    }

    // Check for exception (includes timeout errors)
    if (std::holds_alternative<std::exception_ptr>(state_->result)) {
      auto& eptr = std::get<std::exception_ptr>(state_->result);
      if (eptr) {
        std::rethrow_exception(eptr);
      }
    }

    // Return result
    if constexpr (std::is_void_v<ResultType>) {
      return;
    } else {
      return std::move(std::get<0>(state_->result));
    }
  }

 private:
  static void OnTimeout(uv_timer_t* t) {
    auto* state = static_cast<State*>(t->data);
    if (state->completed) {
      return;  // CPU job already finished
    }
    state->completed = true;
    state->result = std::make_exception_ptr(
        std::runtime_error("Node execution timeout"));
    state->timer = nullptr;

    uv_close(reinterpret_cast<uv_handle_t*>(t),
             [](uv_handle_t* h) { delete reinterpret_cast<uv_timer_t*>(h); });

    state->handle.resume();
  }

  EventLoop& loop_;
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  F fn_;
  std::shared_ptr<State> state_;
};

// Deduction guide for OffloadCpuWithTimeout
template <typename F>
OffloadCpuWithTimeout(EventLoop&, std::optional<std::chrono::steady_clock::time_point>, F&&)
    -> OffloadCpuWithTimeout<std::decay_t<F>>;

}  // namespace ranking
