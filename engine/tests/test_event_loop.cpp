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

  constexpr int NUM_POSTS = 100;

  for (int i = 0; i < NUM_POSTS; ++i) {
    loop.Post([&counter, &done, i]() {
      counter.fetch_add(1);
      if (i == NUM_POSTS - 1) {
        done.set_value();
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
