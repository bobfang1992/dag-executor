#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

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

TEST_CASE("Stress: many concurrent sleeps", "[event_loop][stress][coroutine]") {
  EventLoop loop;
  loop.Start();

  constexpr int NUM_SLEEPS = 50;
  constexpr int SLEEP_MS = 20;

  std::atomic<int> completed{0};
  std::promise<void> all_done;
  auto all_done_future = all_done.get_future();
  std::atomic<bool> signaled{false};

  // Store tasks to keep them alive
  std::vector<Task<void>> tasks;
  tasks.reserve(NUM_SLEEPS);

  // Create all coroutines first
  for (int i = 0; i < NUM_SLEEPS; ++i) {
    auto wrapper = [&loop, &completed, &all_done, &signaled]() -> Task<void> {
      co_await SleepMs(loop, SLEEP_MS);
      int prev = completed.fetch_add(1);
      if (prev == NUM_SLEEPS - 1) {
        bool expected = false;
        if (signaled.compare_exchange_strong(expected, true)) {
          all_done.set_value();
        }
      }
    };
    tasks.push_back(wrapper());
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

  REQUIRE(completed.load() == NUM_SLEEPS);
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
