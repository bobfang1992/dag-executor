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

}  // namespace ranking
