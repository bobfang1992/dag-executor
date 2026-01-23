#pragma once

#include <uv.h>

#include <coroutine>

#include "event_loop.h"

namespace ranking {

// Internal state for a sleep operation.
// Allocated on the heap and self-destructs after the timer closes.
struct SleepState {
  uv_timer_t timer;
  std::coroutine_handle<> handle;

  static void OnTimer(uv_timer_t* t) {
    auto* state = reinterpret_cast<SleepState*>(t);
    auto h = state->handle;

    // Stop the timer and close the handle
    uv_timer_stop(t);
    uv_close(reinterpret_cast<uv_handle_t*>(t), OnClose);

    // Resume the coroutine
    h.resume();
  }

  static void OnClose(uv_handle_t* h) {
    auto* state = reinterpret_cast<SleepState*>(h);
    delete state;
  }
};

// Awaitable that suspends the coroutine for a given number of milliseconds
// using a libuv timer on the event loop.
class SleepAwaitable {
public:
  SleepAwaitable(EventLoop& loop, uint64_t ms) : loop_(loop), ms_(ms) {}

  bool await_ready() const noexcept { return ms_ == 0; }

  bool await_suspend(std::coroutine_handle<> h) {
    // Allocate state on heap - it will self-destruct after close
    auto* state = new SleepState{};
    state->handle = h;

    // Capture loop pointer and ms by value (SleepAwaitable might be destroyed)
    auto* loop_ptr = &loop_;
    auto ms = ms_;

    // Initialize and start timer on the loop thread
    bool posted = loop_.Post([loop_ptr, state, ms]() {
      uv_timer_init(loop_ptr->RawLoop(), &state->timer);
      uv_timer_start(&state->timer, SleepState::OnTimer, ms, 0);
    });

    if (!posted) {
      // Loop not running - clean up and don't suspend
      delete state;
      return false;
    }
    return true;
  }

  void await_resume() noexcept {}

private:
  EventLoop& loop_;
  uint64_t ms_;
};

// Factory function for cleaner syntax
inline SleepAwaitable SleepMs(EventLoop& loop, uint64_t ms) {
  return SleepAwaitable(loop, ms);
}

}  // namespace ranking
