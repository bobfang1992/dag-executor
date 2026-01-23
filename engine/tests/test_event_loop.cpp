#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "coro_task.h"
#include "event_loop.h"
#include "uv_sleep.h"

using namespace ranking;

// Helper to run a Task<T> to completion and get the result
template <typename T>
T blockingWait(EventLoop& loop, Task<T> task) {
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  // Create a wrapper coroutine that signals when done
  auto wrapper = [&]() -> Task<void> {
    co_await std::move(task);
    done_promise.set_value();
  };

  auto wrapper_task = wrapper();

  // Start the wrapper on the loop thread
  loop.Post([&wrapper_task]() { wrapper_task.start(); });

  // Wait for completion
  done_future.wait();
  wrapper_task.result();  // Propagate exceptions

  return T{};  // For void specialization
}

// Specialization for non-void types
template <typename T>
  requires(!std::is_void_v<T>)
T blockingWaitValue(EventLoop& loop, Task<T> task) {
  std::promise<T> result_promise;
  auto result_future = result_promise.get_future();

  // Create a wrapper coroutine that captures the result
  auto wrapper = [&]() -> Task<void> {
    try {
      T result = co_await std::move(task);
      result_promise.set_value(std::move(result));
    } catch (...) {
      result_promise.set_exception(std::current_exception());
    }
  };

  auto wrapper_task = wrapper();

  // Start the wrapper on the loop thread
  loop.Post([&wrapper_task]() { wrapper_task.start(); });

  // Wait for and return the result
  return result_future.get();
}

TEST_CASE("EventLoop basic post", "[event_loop]") {
  EventLoop loop;
  loop.Start();

  std::promise<int> p;
  auto f = p.get_future();

  loop.Post([&p]() { p.set_value(42); });

  REQUIRE(f.get() == 42);

  loop.Stop();
}

TEST_CASE("EventLoop multiple posts", "[event_loop]") {
  EventLoop loop;
  loop.Start();

  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<bool> done_signaled{false};

  constexpr int NUM_POSTS = 100;

  for (int i = 0; i < NUM_POSTS; ++i) {
    loop.Post([&counter, &done, &done_signaled]() {
      int prev = counter.fetch_add(1);
      // Signal when counter reaches NUM_POSTS (not based on index)
      if (prev == NUM_POSTS - 1) {
        bool expected = false;
        if (done_signaled.compare_exchange_strong(expected, true)) {
          done.set_value();
        }
      }
    });
  }

  done_future.wait();
  REQUIRE(counter.load() == NUM_POSTS);

  loop.Stop();
}

TEST_CASE("Single SleepMs coroutine", "[event_loop][coroutine]") {
  EventLoop loop;
  loop.Start();

  auto sleeper = [&loop]() -> Task<int> {
    co_await SleepMs(loop, 50);
    co_return 123;
  };

  auto start = std::chrono::steady_clock::now();
  int result = blockingWaitValue(loop, sleeper());
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  REQUIRE(result == 123);
  REQUIRE(elapsed.count() >= 40);   // Allow some timing variance
  REQUIRE(elapsed.count() < 150);   // But not too long

  loop.Stop();
}

TEST_CASE("Two concurrent SleepMs complete in parallel", "[event_loop][coroutine]") {
  EventLoop loop;
  loop.Start();

  // Two coroutines each sleeping 50ms should complete in ~50ms total, not ~100ms
  auto sleeper = [&loop](int id) -> Task<int> {
    co_await SleepMs(loop, 50);
    co_return id;
  };

  std::promise<int> p1, p2;
  auto f1 = p1.get_future();
  auto f2 = p2.get_future();

  auto wrapper1 = [&]() -> Task<void> {
    try {
      int result = co_await sleeper(1);
      p1.set_value(result);
    } catch (...) {
      p1.set_exception(std::current_exception());
    }
  };

  auto wrapper2 = [&]() -> Task<void> {
    try {
      int result = co_await sleeper(2);
      p2.set_value(result);
    } catch (...) {
      p2.set_exception(std::current_exception());
    }
  };

  auto task1 = wrapper1();
  auto task2 = wrapper2();

  auto start = std::chrono::steady_clock::now();

  // Start both coroutines concurrently
  loop.Post([&task1]() { task1.start(); });
  loop.Post([&task2]() { task2.start(); });

  // Wait for both to complete
  int r1 = f1.get();
  int r2 = f2.get();

  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  REQUIRE(r1 == 1);
  REQUIRE(r2 == 2);

  // Critical assertion: should complete in ~50ms, not ~100ms
  // Allow generous bounds for CI timing variance
  REQUIRE(elapsed.count() >= 40);
  REQUIRE(elapsed.count() < 120);  // Less than 2x the sleep time proves parallelism

  loop.Stop();
}

TEST_CASE("Exception propagation in coroutine", "[event_loop][coroutine]") {
  EventLoop loop;
  loop.Start();

  auto thrower = [&loop]() -> Task<int> {
    co_await SleepMs(loop, 10);
    throw std::runtime_error("test exception");
    co_return 0;  // Never reached
  };

  std::promise<int> result_promise;
  auto result_future = result_promise.get_future();

  auto wrapper = [&]() -> Task<void> {
    try {
      int result = co_await thrower();
      result_promise.set_value(result);
    } catch (...) {
      result_promise.set_exception(std::current_exception());
    }
  };

  auto wrapper_task = wrapper();
  loop.Post([&wrapper_task]() { wrapper_task.start(); });

  REQUIRE_THROWS_AS(result_future.get(), std::runtime_error);

  loop.Stop();
}

TEST_CASE("Zero sleep completes immediately", "[event_loop][coroutine]") {
  EventLoop loop;
  loop.Start();

  auto instant = [&loop]() -> Task<int> {
    co_await SleepMs(loop, 0);  // Should not actually suspend
    co_return 99;
  };

  auto start = std::chrono::steady_clock::now();
  int result = blockingWaitValue(loop, instant());
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  REQUIRE(result == 99);
  REQUIRE(elapsed.count() < 50);  // Should be nearly instant

  loop.Stop();
}

TEST_CASE("Nested coroutine awaits", "[event_loop][coroutine]") {
  EventLoop loop;
  loop.Start();

  auto inner = [&loop]() -> Task<int> {
    co_await SleepMs(loop, 20);
    co_return 10;
  };

  auto outer = [&loop, &inner]() -> Task<int> {
    int a = co_await inner();
    co_await SleepMs(loop, 20);
    int b = co_await inner();
    co_return a + b;
  };

  auto start = std::chrono::steady_clock::now();
  int result = blockingWaitValue(loop, outer());
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  REQUIRE(result == 20);  // 10 + 10
  // Sequential: 20 + 20 + 20 = 60ms total
  REQUIRE(elapsed.count() >= 50);
  REQUIRE(elapsed.count() < 150);

  loop.Stop();
}

TEST_CASE("Post before Start returns false", "[event_loop]") {
  EventLoop loop;
  // Don't call Start()

  bool posted = loop.Post([]() {});
  REQUIRE_FALSE(posted);
}

TEST_CASE("Post after Stop returns false", "[event_loop]") {
  EventLoop loop;
  loop.Start();
  loop.Stop();

  bool posted = loop.Post([]() {});
  REQUIRE_FALSE(posted);
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_CASE("Stress: many concurrent posts", "[event_loop][stress]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_POSTS = 10000;
  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<bool> done_signaled{false};

  for (int i = 0; i < NUM_POSTS; ++i) {
    loop.Post([&counter, &done, &done_signaled]() {
      int prev = counter.fetch_add(1);
      if (prev == NUM_POSTS - 1) {
        bool expected = false;
        if (done_signaled.compare_exchange_strong(expected, true)) {
          done.set_value();
        }
      }
    });
  }

  done_future.wait();
  REQUIRE(counter.load() == NUM_POSTS);

  loop.Stop();
}

TEST_CASE("Stress: posts from multiple threads", "[event_loop][stress]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_THREADS = 8;
  constexpr int POSTS_PER_THREAD = 1000;
  constexpr int TOTAL_POSTS = NUM_THREADS * POSTS_PER_THREAD;

  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<bool> done_signaled{false};

  std::vector<std::thread> threads;
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&loop, &counter, &done, &done_signaled]() {
      for (int i = 0; i < POSTS_PER_THREAD; ++i) {
        loop.Post([&counter, &done, &done_signaled]() {
          int prev = counter.fetch_add(1);
          if (prev == TOTAL_POSTS - 1) {
            bool expected = false;
            if (done_signaled.compare_exchange_strong(expected, true)) {
              done.set_value();
            }
          }
        });
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  done_future.wait();
  REQUIRE(counter.load() == TOTAL_POSTS);

  loop.Stop();
}

// Helper struct for tracking completion across multiple coroutines
struct CompletionTracker {
  std::atomic<int> completed{0};
  std::promise<void> all_done;
  std::atomic<bool> signaled{false};
  int total_count;

  explicit CompletionTracker(int count) : total_count(count) {}

  void mark_complete() {
    int prev = completed.fetch_add(1);
    if (prev == total_count - 1) {
      bool expected = false;
      if (signaled.compare_exchange_strong(expected, true)) {
        all_done.set_value();
      }
    }
  }
};

// Helper coroutine for stress test - parameters are copied into frame
static Task<void> makeStressSleeper(EventLoop& loop, CompletionTracker& tracker, int sleep_ms) {
  co_await SleepMs(loop, sleep_ms);
  tracker.mark_complete();
}

TEST_CASE("Stress: many concurrent sleeps", "[event_loop][stress][coroutine]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_SLEEPS = 50;
  constexpr int SLEEP_MS = 20;

  CompletionTracker tracker(NUM_SLEEPS);
  auto all_done_future = tracker.all_done.get_future();

  // Store tasks to keep them alive
  std::vector<Task<void>> tasks;
  tasks.reserve(NUM_SLEEPS);

  // Create all coroutines first using helper function
  // (helper function parameters are copied into coroutine frame, avoiding lifetime issues)
  for (int i = 0; i < NUM_SLEEPS; ++i) {
    tasks.push_back(makeStressSleeper(loop, tracker, SLEEP_MS));
  }

  auto start = std::chrono::steady_clock::now();

  // Start all coroutines
  for (auto& task : tasks) {
    loop.Post([&task]() { task.start(); });
  }

  // Wait for all to complete
  all_done_future.wait();

  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  REQUIRE(tracker.completed.load() == NUM_SLEEPS);
  // All sleeps should complete in ~20-80ms (parallel), not 1000ms (sequential)
  REQUIRE(elapsed.count() < 200);

  loop.Stop();
}

TEST_CASE("Stress: rapid start/stop cycles", "[event_loop][stress]") {
  for (int i = 0; i < 50; ++i) {
    EventLoop loop;
    loop.Start();

    std::promise<void> p;
    auto f = p.get_future();

    loop.Post([&p]() { p.set_value(); });

    f.wait();
    loop.Stop();
  }
  // If we get here without crashing, the test passes
  REQUIRE(true);
}

TEST_CASE("Stop from within callback", "[event_loop][edge_case]") {
  auto loop = std::make_unique<EventLoop>();
  loop->Start();

  std::promise<void> stopped;
  auto stopped_future = stopped.get_future();

  // Post a callback that calls Stop() from within the loop thread
  loop->Post([&loop, &stopped]() {
    loop->Stop();
    stopped.set_value();
  });

  stopped_future.wait();

  // Destructor should not deadlock or crash
  loop.reset();

  REQUIRE(true);
}

// NOTE: "Destruction on loop thread" is NOT supported.
// Callbacks must not destroy the EventLoop - this would cause UAF because
// uv_run() is still on the stack. Destructor asserts if called from loop thread.

TEST_CASE("Multiple Stop calls are idempotent", "[event_loop][edge_case]") {
  EventLoop loop;
  loop.Start();

  std::promise<void> p;
  auto f = p.get_future();

  loop.Post([&p]() { p.set_value(); });
  f.wait();

  // Multiple Stop calls should be safe
  loop.Stop();
  loop.Stop();
  loop.Stop();

  REQUIRE_FALSE(loop.IsRunning());
}

TEST_CASE("Destruction without Stop", "[event_loop][edge_case]") {
  {
    EventLoop loop;
    loop.Start();

    std::promise<void> p;
    auto f = p.get_future();

    loop.Post([&p]() { p.set_value(); });
    f.wait();

    // Don't call Stop() - destructor should handle it
  }
  // If we get here without hanging or crashing, the test passes
  REQUIRE(true);
}

TEST_CASE("Destruction without Start", "[event_loop][edge_case]") {
  {
    EventLoop loop;
    // Don't call Start() - just let destructor run
  }
  REQUIRE(true);
}

TEST_CASE("Post during Stop is rejected", "[event_loop][edge_case]") {
  EventLoop loop;
  loop.Start();

  std::atomic<bool> stop_started{false};
  std::atomic<int> rejected_count{0};
  std::atomic<bool> stopper_done{false};

  // Thread that will call Stop
  std::thread stopper([&]() {
    stop_started.store(true);
    loop.Stop();
    stopper_done.store(true);
  });

  // Wait for stop to start
  while (!stop_started.load()) {
    std::this_thread::yield();
  }

  // Try to post while stopping - some may succeed, some may be rejected
  for (int i = 0; i < 100; ++i) {
    if (!loop.Post([]() {})) {
      rejected_count.fetch_add(1);
    }
  }

  stopper.join();

  // After Stop completes, all posts should be rejected
  REQUIRE_FALSE(loop.Post([]() {}));
  // At least some posts during shutdown should have been rejected
  // (unless they all snuck in before stopping_ was set)
  INFO("Rejected during stop: " << rejected_count.load());
}

TEST_CASE("Stop on loop thread drains accepted callbacks", "[event_loop][edge_case]") {
  EventLoop loop;
  loop.Start();

  constexpr int PRODUCER_THREADS = 4;
  constexpr int POSTS_PER_THREAD = 200;

  std::atomic<int> accepted{0};
  std::atomic<int> executed{0};

  std::promise<void> blocker_entered;
  std::promise<void> release_blocker;

  // Block the loop thread so queued callbacks accumulate, then stop from that thread.
  loop.Post([&]() {
    blocker_entered.set_value();
    release_blocker.get_future().wait();
    loop.Stop();
  });

  blocker_entered.get_future().wait();

  // Producers post callbacks while the loop is blocked.
  std::vector<std::thread> threads;
  for (int t = 0; t < PRODUCER_THREADS; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < POSTS_PER_THREAD; ++i) {
        bool ok = loop.Post([&]() { executed.fetch_add(1); });
        if (ok) {
          accepted.fetch_add(1);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Let the blocker proceed and trigger Stop() on the loop thread.
  release_blocker.set_value();

  // Wait for all accepted callbacks to run.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (executed.load() == accepted.load()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  REQUIRE(executed.load() == accepted.load());
  REQUIRE_FALSE(loop.Post([]() {}));
}

// ============================================================================
// Advanced Edge Case Tests
// ============================================================================

// This test documents that uncaught exceptions in callbacks will terminate.
// Tagged as hidden ([.]) so it doesn't run by default - it's expected to crash.
TEST_CASE("Exception in callback terminates", "[event_loop][.exception]") {
  EventLoop loop;
  loop.Start();

  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();

  // Post a callback that throws
  loop.Post([&counter]() {
    counter.fetch_add(1);
    throw std::runtime_error("intentional exception");
  });

  // Post more callbacks after the throwing one
  loop.Post([&counter]() { counter.fetch_add(1); });
  loop.Post([&counter]() { counter.fetch_add(1); });
  loop.Post([&counter, &done]() {
    counter.fetch_add(1);
    done.set_value();
  });

  // Note: The exception will likely terminate the program or be unhandled
  // This test documents current behavior - exceptions in callbacks are UB
  // For now, just verify the loop can be stopped
  // In production, callbacks should catch their own exceptions

  // Give some time for callbacks to run
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  loop.Stop();

  // We got here without crashing
  REQUIRE(true);
}

// Helper function for creating sleep coroutines.
// IMPORTANT: Coroutine function parameters are copied into the coroutine frame,
// so they remain valid even after the calling context goes out of scope.
// This is NOT true for lambda captures - they reference the lambda's internal
// storage which is destroyed when the lambda goes out of scope!
static Task<void> makeSleeper(EventLoop& loop, std::atomic<int>& started,
                              std::atomic<int>& completed, int sleep_ms) {
  started.fetch_add(1);
  co_await SleepMs(loop, sleep_ms);
  completed.fetch_add(1);
}

// Simpler helper that just sleeps and increments a counter
static Task<void> makeSimpleSleeper(EventLoop& loop, std::atomic<int>& counter, int sleep_ms) {
  co_await SleepMs(loop, sleep_ms);
  counter.fetch_add(1);
}

TEST_CASE("Stop during active sleep coroutines", "[event_loop][edge_case][coroutine]") {
  EventLoop loop;
  loop.Start();

  std::atomic<int> started{0};
  std::atomic<int> completed{0};

  // Start several long-running sleep coroutines
  // IMPORTANT: Use a helper function instead of lambda to avoid lifetime issues.
  // Lambda captures reference the lambda's storage, not the original variables,
  // so they become dangling when the lambda goes out of scope at iteration end.
  std::vector<Task<void>> tasks;
  for (int i = 0; i < 10; ++i) {
    tasks.push_back(makeSleeper(loop, started, completed, 5000));
  }

  // Start all coroutines
  for (auto& task : tasks) {
    loop.Post([&task]() { task.start(); });
  }

  // Wait for all to start
  while (started.load() < 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Stop the loop while sleeps are pending
  loop.Stop();

  // Sleeps should NOT have completed (we stopped early)
  REQUIRE(started.load() == 10);
  REQUIRE(completed.load() == 0);
}

TEST_CASE("Rapid timer creation and cancellation", "[event_loop][stress][coroutine]") {
  EventLoop loop;
  loop.Start();

  std::atomic<int> completed{0};
  constexpr int NUM_TIMERS = 500;

  // Create many short timers rapidly
  std::vector<Task<void>> tasks;
  tasks.reserve(NUM_TIMERS);

  // Use helper function to avoid lambda capture lifetime issues
  for (int i = 0; i < NUM_TIMERS; ++i) {
    tasks.push_back(makeSimpleSleeper(loop, completed, 1));
  }

  // Start all
  for (auto& task : tasks) {
    loop.Post([&task]() { task.start(); });
  }

  // Wait for completion
  auto start = std::chrono::steady_clock::now();
  while (completed.load() < NUM_TIMERS) {
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(5)) {
      FAIL("Timeout waiting for timers");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE(completed.load() == NUM_TIMERS);
  loop.Stop();
}

TEST_CASE("Nested Post from callback", "[event_loop][edge_case]") {
  EventLoop loop;
  loop.Start();

  std::atomic<int> depth{0};
  std::atomic<int> max_depth{0};
  std::promise<void> done;
  auto done_future = done.get_future();

  constexpr int TARGET_DEPTH = 100;

  std::function<void()> nested_post;
  nested_post = [&]() {
    int current = depth.fetch_add(1) + 1;
    int expected = max_depth.load();
    while (current > expected && !max_depth.compare_exchange_weak(expected, current)) {}

    if (current < TARGET_DEPTH) {
      loop.Post(nested_post);
    } else {
      done.set_value();
    }
  };

  loop.Post(nested_post);
  done_future.wait();

  REQUIRE(max_depth.load() == TARGET_DEPTH);
  loop.Stop();
}

TEST_CASE("Post from destructor of posted object", "[event_loop][edge_case]") {
  EventLoop loop;
  loop.Start();

  std::atomic<int> destructor_count{0};
  std::atomic<bool> done_signaled{false};
  std::promise<void> done;
  auto done_future = done.get_future();

  constexpr int NUM_OBJECTS = 10;

  // Use a simple shared_ptr to a counter that signals when all are destroyed
  auto signal_on_destroy = [&loop, &destructor_count, &done, &done_signaled]() {
    int count = destructor_count.fetch_add(1) + 1;
    if (count == NUM_OBJECTS) {
      bool expected = false;
      if (done_signaled.compare_exchange_strong(expected, true)) {
        // Post the signal to ensure we're testing that posts from destructors work
        loop.Post([&done]() { done.set_value(); });
      }
    }
  };

  // Post callbacks that call signal_on_destroy when lambda is destroyed
  for (int i = 0; i < NUM_OBJECTS; ++i) {
    // Capture a shared_ptr to ensure the destructor runs when the lambda is destroyed
    auto destructor_trigger = std::make_shared<int>(0);
    // Custom destructor via weak_ptr check in a nested shared_ptr
    auto destroyer = std::shared_ptr<void>(nullptr, [signal_on_destroy](void*) {
      signal_on_destroy();
    });

    loop.Post([destroyer]() {
      // Destructor runs when lambda (and thus shared_ptr) is destroyed
    });
  }

  done_future.wait();
  REQUIRE(destructor_count.load() == NUM_OBJECTS);
  loop.Stop();
}

// State for tracking interleaved sleep completion order
struct InterleavedState {
  std::vector<int> completion_order;
  std::mutex order_mutex;
  std::promise<void> done;
  std::atomic<int> completed{0};
  int total_count;

  explicit InterleavedState(int count) : total_count(count) {}

  void record_completion(int id) {
    {
      std::lock_guard<std::mutex> lock(order_mutex);
      completion_order.push_back(id);
    }
    if (completed.fetch_add(1) == total_count - 1) {
      done.set_value();
    }
  }
};

// Helper coroutine for interleaved sleep test
static Task<void> makeInterleavedSleeper(EventLoop& loop, InterleavedState& state, int id, int ms) {
  co_await SleepMs(loop, ms);
  state.record_completion(id);
}

TEST_CASE("Interleaved sleeps with different durations", "[event_loop][coroutine]") {
  EventLoop loop;
  loop.Start();

  InterleavedState state(5);
  auto done_future = state.done.get_future();

  // IDs and durations: expect completion order based on duration
  std::vector<std::pair<int, int>> sleeps = {{1, 50}, {2, 10}, {3, 30}, {4, 20}, {5, 40}};
  std::vector<Task<void>> tasks;
  for (auto [id, ms] : sleeps) {
    tasks.push_back(makeInterleavedSleeper(loop, state, id, ms));
  }

  // Start all
  for (auto& task : tasks) {
    loop.Post([&task]() { task.start(); });
  }

  done_future.wait();

  // Check completion order matches duration order: 2(10), 4(20), 3(30), 5(40), 1(50)
  std::vector<int> expected = {2, 4, 3, 5, 1};
  REQUIRE(state.completion_order == expected);

  loop.Stop();
}

TEST_CASE("High contention multi-producer", "[event_loop][stress]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_THREADS = 16;
  constexpr int POSTS_PER_THREAD = 500;
  constexpr int TOTAL = NUM_THREADS * POSTS_PER_THREAD;

  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<bool> signaled{false};

  std::vector<std::thread> threads;
  std::atomic<bool> start_flag{false};

  // Create threads that all wait then post simultaneously
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&]() {
      while (!start_flag.load()) {
        std::this_thread::yield();
      }
      for (int i = 0; i < POSTS_PER_THREAD; ++i) {
        loop.Post([&]() {
          int prev = counter.fetch_add(1);
          if (prev == TOTAL - 1) {
            bool expected = false;
            if (signaled.compare_exchange_strong(expected, true)) {
              done.set_value();
            }
          }
        });
      }
    });
  }

  // Start all threads simultaneously
  start_flag.store(true);

  // Wait for completion
  auto status = done_future.wait_for(std::chrono::seconds(10));
  REQUIRE(status == std::future_status::ready);

  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(counter.load() == TOTAL);
  loop.Stop();
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_CASE("Perf: high-volume multi-producer throughput", "[event_loop][perf]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_THREADS = 8;
  constexpr int POSTS_PER_THREAD = 10000;
  constexpr int TOTAL = NUM_THREADS * POSTS_PER_THREAD;

  std::atomic<int> counter{0};
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<bool> signaled{false};

  std::vector<std::thread> threads;
  std::atomic<bool> start_flag{false};

  auto start = std::chrono::steady_clock::now();

  // Producers wait until signaled, then post in parallel
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&]() {
      while (!start_flag.load()) {
        std::this_thread::yield();
      }
      for (int i = 0; i < POSTS_PER_THREAD; ++i) {
        loop.Post([&]() {
          int prev = counter.fetch_add(1);
          if (prev == TOTAL - 1) {
            bool expected = false;
            if (signaled.compare_exchange_strong(expected, true)) {
              done.set_value();
            }
          }
        });
      }
    });
  }

  start_flag.store(true);

  auto status = done_future.wait_for(std::chrono::seconds(10));
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
  INFO("Throughput elapsed ms: " << elapsed_ms);

  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(status == std::future_status::ready);
  REQUIRE(counter.load() == TOTAL);
  REQUIRE(elapsed_ms < 5000);  // Should complete well under 5s on typical hosts

  loop.Stop();
}

TEST_CASE("Perf: short timers stay within latency budget", "[event_loop][perf][coroutine]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_TIMERS = 200;
  constexpr int SLEEP_MS = 2;

  CompletionTracker tracker(NUM_TIMERS);
  auto all_done_future = tracker.all_done.get_future();

  std::vector<Task<void>> tasks;
  tasks.reserve(NUM_TIMERS);
  for (int i = 0; i < NUM_TIMERS; ++i) {
    tasks.push_back(makeStressSleeper(loop, tracker, SLEEP_MS));
  }

  auto start = std::chrono::steady_clock::now();
  for (auto& task : tasks) {
    loop.Post([&task]() { task.start(); });
  }

  auto status = all_done_future.wait_for(std::chrono::seconds(3));
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
  INFO("Timer batch elapsed ms: " << elapsed_ms);

  REQUIRE(status == std::future_status::ready);
  REQUIRE(tracker.completed.load() == NUM_TIMERS);
  REQUIRE(elapsed_ms < 500);  // Generous budget; flags regressions without flaking

  loop.Stop();
}

TEST_CASE("Perf: start/stop with workload bursts", "[event_loop][perf]") {
  constexpr int ITERATIONS = 20;
  constexpr int POSTS_PER_ITERATION = 200;

  auto total_start = std::chrono::steady_clock::now();

  for (int iter = 0; iter < ITERATIONS; ++iter) {
    EventLoop loop;
    loop.Start();

    std::atomic<int> counter{0};
    std::promise<void> done;
    auto done_future = done.get_future();
    std::atomic<bool> signaled{false};

    for (int i = 0; i < POSTS_PER_ITERATION; ++i) {
      loop.Post([&]() {
        int prev = counter.fetch_add(1);
        if (prev == POSTS_PER_ITERATION - 1) {
          bool expected = false;
          if (signaled.compare_exchange_strong(expected, true)) {
            done.set_value();
          }
        }
      });
    }

    auto status = done_future.wait_for(std::chrono::seconds(2));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(counter.load() == POSTS_PER_ITERATION);

    loop.Stop();
  }

  auto total_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - total_start)
                              .count();
  INFO("Start/stop burst total elapsed ms: " << total_elapsed_ms);
  REQUIRE(total_elapsed_ms < 5000);
}
