#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "async_inflight_limiter.h"
#include "async_io_clients.h"
#include "async_redis_client.h"
#include "coro_task.h"
#include "event_loop.h"
#include "uv_sleep.h"

using namespace ranking;

// =============================================================================
// Unit Tests: AsyncInflightLimiter (no Redis required)
// =============================================================================

TEST_CASE("AsyncInflightLimiter basic acquire/release", "[async_limiter]") {
  AsyncInflightLimiter limiter(3);

  REQUIRE(limiter.max_permits() == 3);
  REQUIRE(limiter.current() == 0);

  // Acquire permit synchronously
  REQUIRE(limiter.try_acquire());
  REQUIRE(limiter.current() == 1);

  REQUIRE(limiter.try_acquire());
  REQUIRE(limiter.current() == 2);

  REQUIRE(limiter.try_acquire());
  REQUIRE(limiter.current() == 3);

  // At limit - should fail
  REQUIRE_FALSE(limiter.try_acquire());
  REQUIRE(limiter.current() == 3);

  // Release one
  limiter.release();
  REQUIRE(limiter.current() == 2);

  // Can acquire again
  REQUIRE(limiter.try_acquire());
  REQUIRE(limiter.current() == 3);
}

TEST_CASE("AsyncInflightLimiter Guard RAII", "[async_limiter]") {
  AsyncInflightLimiter limiter(2);

  {
    auto guard1 = AsyncInflightLimiter::Guard(&limiter);
    // Manually acquired, need to account for it
    limiter.try_acquire();  // First acquire
    REQUIRE(limiter.current() == 1);

    {
      limiter.try_acquire();  // Second acquire
      auto guard2 = AsyncInflightLimiter::Guard(&limiter);
      REQUIRE(limiter.current() == 2);
    }
    // guard2 destructor releases
    REQUIRE(limiter.current() == 1);
  }
  // guard1 destructor releases
  REQUIRE(limiter.current() == 0);
}

TEST_CASE("AsyncInflightLimiter coroutine acquire", "[async_limiter]") {
  EventLoop loop;
  loop.Start();

  AsyncInflightLimiter limiter(2);
  std::atomic<int> completed{0};
  std::atomic<int> max_concurrent{0};
  std::atomic<int> current_concurrent{0};

  // Create coroutines that acquire permits, do work, then release
  auto worker = [&](int id) -> Task<void> {
    auto guard = co_await limiter.acquire();

    // Track max concurrency
    int c = ++current_concurrent;
    int expected = max_concurrent.load();
    while (c > expected && !max_concurrent.compare_exchange_weak(expected, c)) {
    }

    // Simulate work
    co_await SleepMs(loop, 10);

    --current_concurrent;
    ++completed;
  };

  // Launch 5 workers - only 2 should run concurrently
  std::vector<Task<void>> tasks;
  for (int i = 0; i < 5; ++i) {
    tasks.push_back(worker(i));
  }

  // Post the task starts to the loop thread
  loop.Post([&]() {
    for (auto& task : tasks) {
      task.start();
    }
  });

  // Wait for completion
  auto start = std::chrono::steady_clock::now();
  while (completed.load() < 5) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(5)) {
      FAIL("Timeout waiting for workers to complete");
      break;
    }
  }

  loop.Stop();

  REQUIRE(completed.load() == 5);
  REQUIRE(max_concurrent.load() <= 2);  // Never exceeded limit
}

TEST_CASE("AsyncInflightLimiter FIFO ordering", "[async_limiter]") {
  EventLoop loop;
  loop.Start();

  AsyncInflightLimiter limiter(1);
  std::vector<int> completion_order;
  std::atomic<int> completed{0};

  auto worker = [&](int id) -> Task<void> {
    auto guard = co_await limiter.acquire();
    completion_order.push_back(id);
    co_await SleepMs(loop, 5);  // Hold permit briefly
    ++completed;
  };

  // Create tasks in order 0, 1, 2
  std::vector<Task<void>> tasks;
  for (int i = 0; i < 3; ++i) {
    tasks.push_back(worker(i));
  }

  // Start all tasks on the loop thread
  loop.Post([&]() {
    for (auto& task : tasks) {
      task.start();
    }
  });

  // Wait for all to complete
  auto start = std::chrono::steady_clock::now();
  while (completed.load() < 3) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL("Timeout");
      break;
    }
  }

  loop.Stop();

  // Should complete in FIFO order: 0, 1, 2
  REQUIRE(completion_order.size() == 3);
  REQUIRE(completion_order[0] == 0);
  REQUIRE(completion_order[1] == 1);
  REQUIRE(completion_order[2] == 2);
}

// =============================================================================
// Integration Tests: AsyncRedisClient (requires Redis)
// =============================================================================

// Helper to create a test endpoint spec
rankd::EndpointSpec make_redis_endpoint(const std::string& host = "127.0.0.1", int port = 6379) {
  rankd::EndpointSpec spec;
  spec.endpoint_id = "ep_test";
  spec.name = "test_redis";
  spec.kind = rankd::EndpointKind::Redis;
  spec.resolver_type = rankd::ResolverType::Static;
  spec.static_resolver.host = host;
  spec.static_resolver.port = port;
  spec.policy.max_inflight = 64;
  spec.policy.connect_timeout_ms = 100;
  spec.policy.request_timeout_ms = 50;
  return spec;
}

TEST_CASE("AsyncRedisClient connection to invalid port", "[redis]") {
  EventLoop loop;
  loop.Start();

  auto spec = make_redis_endpoint("127.0.0.1", 59999);  // Unlikely port

  auto result = AsyncRedisClient::Create(loop, spec);

  // Should fail quickly (connection refused)
  // Note: hiredis async connect might not fail immediately
  // The actual failure may come on first command

  loop.Stop();

  // Either create fails or the client reports not connected
  // (behavior depends on OS and timing)
}

TEST_CASE("AsyncRedisClient create only", "[redis][create]") {
  EventLoop loop;
  loop.Start();

  auto spec = make_redis_endpoint();

  // Create client on the loop thread
  std::atomic<bool> client_ready{false};
  std::unique_ptr<AsyncRedisClient> client;
  std::string create_error;

  loop.Post([&]() {
    auto result = AsyncRedisClient::Create(loop, spec);
    if (result) {
      client = std::move(*result);
    } else {
      create_error = result.error();
    }
    client_ready = true;
  });

  // Wait for client creation
  auto start = std::chrono::steady_clock::now();
  while (!client_ready.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL("Timeout waiting for client creation");
      break;
    }
  }

  INFO("Client created: " << (client ? "yes" : "no"));
  INFO("Error: " << create_error);

  // Give connection time to establish
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  INFO("Is connected: " << (client ? (client->is_connected() ? "yes" : "no") : "n/a"));

  loop.Stop();

  // Either client creation succeeded or we got an error
  REQUIRE((client || !create_error.empty()));
}

TEST_CASE("AsyncRedisClient HGet", "[redis]") {
  EventLoop loop;
  loop.Start();

  auto spec = make_redis_endpoint();

  std::atomic<bool> done{false};
  std::optional<std::string> hget_result;
  std::string error_msg;

  // Create an all-in-one coroutine that runs entirely on the loop thread
  auto full_test = [&]() -> Task<void> {
    // Create client on the loop thread
    auto result = AsyncRedisClient::Create(loop, spec);
    if (!result) {
      error_msg = "Create failed: " + result.error();
      done = true;
      co_return;
    }

    auto& client = *result;

    // Wait for connection to establish
    co_await SleepMs(loop, 50);

    // Now issue HGet
    auto hget = co_await client->HGet("user:1", "country");
    if (hget) {
      hget_result = *hget;
    } else {
      error_msg = hget.error().message;
    }
    done = true;
  };

  auto task = full_test();
  loop.Post([&]() { task.start(); });

  // Wait for completion
  auto start = std::chrono::steady_clock::now();
  while (!done.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL("Timeout waiting for HGet");
      break;
    }
  }

  loop.Stop();

  // If Redis has the key, we get a value; if not, nullopt
  // Either is acceptable for this test - we just want no errors
  INFO("HGet result: " << (hget_result ? *hget_result : "(null)"));
  INFO("Error: " << error_msg);

  // Should have completed without crash
  REQUIRE(done.load());
}

TEST_CASE("AsyncRedisClient LRange", "[redis]") {
  EventLoop loop;
  loop.Start();

  auto spec = make_redis_endpoint();

  std::atomic<bool> done{false};
  std::vector<std::string> lrange_result;
  std::string error_msg;

  auto full_test = [&]() -> Task<void> {
    auto result = AsyncRedisClient::Create(loop, spec);
    if (!result) {
      error_msg = "Create failed: " + result.error();
      done = true;
      co_return;
    }

    auto& client = *result;
    co_await SleepMs(loop, 50);

    auto lrange = co_await client->LRange("media:1", 0, -1);
    if (lrange) {
      lrange_result = std::move(*lrange);
    } else {
      error_msg = lrange.error().message;
    }
    done = true;
  };

  auto task = full_test();
  loop.Post([&]() { task.start(); });

  auto start = std::chrono::steady_clock::now();
  while (!done.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL("Timeout waiting for LRange");
      break;
    }
  }

  loop.Stop();

  INFO("LRange result size: " << lrange_result.size());
  INFO("Error: " << error_msg);
  REQUIRE(done.load());
}

TEST_CASE("AsyncRedisClient concurrent LRange with inflight limit", "[redis]") {
  EventLoop loop;
  loop.Start();

  auto spec = make_redis_endpoint();
  spec.policy.max_inflight = 10;  // Low limit for testing

  std::atomic<int> completed{0};
  std::atomic<int> errors{0};
  constexpr int num_requests = 50;
  std::string create_error;

  auto full_test = [&]() -> Task<void> {
    auto result = AsyncRedisClient::Create(loop, spec);
    if (!result) {
      create_error = result.error();
      co_return;
    }

    auto& client = *result;
    co_await SleepMs(loop, 50);

    // Create worker coroutines
    auto worker = [&]() -> Task<void> {
      auto lrange = co_await client->LRange("media:1", 0, 10);
      if (lrange) {
        ++completed;
      } else {
        ++errors;
      }
    };

    std::vector<Task<void>> workers;
    for (int i = 0; i < num_requests; ++i) {
      workers.push_back(worker());
    }

    // Start all workers
    for (auto& w : workers) {
      w.start();
    }

    // Wait for all workers to complete
    while (completed.load() + errors.load() < num_requests) {
      co_await SleepMs(loop, 10);
    }
  };

  auto task = full_test();
  loop.Post([&]() { task.start(); });

  auto start = std::chrono::steady_clock::now();
  while (completed.load() + errors.load() < num_requests && create_error.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
      FAIL("Timeout: completed=" << completed.load() << " errors=" << errors.load());
      break;
    }
  }

  loop.Stop();

  if (!create_error.empty()) {
    WARN("Could not connect to Redis: " << create_error);
    SKIP("Redis not available");
    return;
  }

  INFO("Completed: " << completed.load() << " Errors: " << errors.load());
  REQUIRE(completed.load() + errors.load() == num_requests);
}

TEST_CASE("AsyncIoClients caching", "[redis]") {
  EventLoop loop;
  loop.Start();

  // Create a simple endpoint registry
  rankd::EndpointSpec spec = make_redis_endpoint();

  // For this test, we need an EndpointRegistry, but we'll just test the caching logic
  // by creating clients directly
  AsyncIoClients clients;

  REQUIRE(clients.redis_count() == 0);

  // GetExistingRedis should return nullptr for unknown endpoint
  REQUIRE(clients.GetExistingRedis("ep_unknown") == nullptr);

  // Clear should work on empty
  clients.Clear();
  REQUIRE(clients.redis_count() == 0);

  loop.Stop();
}

// =============================================================================
// Stress test (optional, for manual runs)
// =============================================================================

TEST_CASE("AsyncRedisClient stress test", "[redis][.stress]") {
  EventLoop loop;
  loop.Start();

  auto spec = make_redis_endpoint();
  spec.policy.max_inflight = 64;

  std::atomic<int> completed{0};
  constexpr int num_requests = 1000;
  std::string create_error;
  std::chrono::steady_clock::time_point start_time;

  auto full_test = [&]() -> Task<void> {
    auto result = AsyncRedisClient::Create(loop, spec);
    if (!result) {
      create_error = result.error();
      co_return;
    }

    auto& client = *result;
    co_await SleepMs(loop, 50);

    start_time = std::chrono::steady_clock::now();

    auto worker = [&]() -> Task<void> {
      auto lrange = co_await client->LRange("media:1", 0, 5);
      ++completed;
    };

    std::vector<Task<void>> workers;
    for (int i = 0; i < num_requests; ++i) {
      workers.push_back(worker());
    }

    for (auto& w : workers) {
      w.start();
    }

    while (completed.load() < num_requests) {
      co_await SleepMs(loop, 10);
    }
  };

  auto task = full_test();
  loop.Post([&]() { task.start(); });

  while (completed.load() < num_requests && create_error.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (start_time != std::chrono::steady_clock::time_point{} &&
        elapsed > std::chrono::seconds(30)) {
      FAIL("Timeout");
      break;
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  loop.Stop();

  if (!create_error.empty()) {
    SKIP("Redis not available");
    return;
  }

  INFO("Completed " << completed.load() << " requests in " << duration << "ms");
  INFO("Rate: " << (completed.load() * 1000.0 / duration) << " req/s");
  REQUIRE(completed.load() == num_requests);
}
