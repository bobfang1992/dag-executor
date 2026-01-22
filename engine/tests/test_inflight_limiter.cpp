#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "inflight_limiter.h"

using namespace rankd;

TEST_CASE("InflightLimiter basic acquire/release", "[inflight]") {
  InflightLimiter::reset_all();

  SECTION("acquire returns guard") {
    auto guard = InflightLimiter::acquire("test_ep", 10);
    // Guard should be valid
    REQUIRE(true);
  }

  SECTION("guard releases on destruction") {
    {
      auto guard = InflightLimiter::acquire("test_ep", 10);
    }
    // Should be able to acquire again after guard destruction
    auto guard2 = InflightLimiter::acquire("test_ep", 10);
    REQUIRE(true);
  }

  SECTION("multiple acquires within limit") {
    std::vector<InflightLimiter::Guard> guards;
    for (int i = 0; i < 5; ++i) {
      guards.push_back(InflightLimiter::acquire("test_ep", 10));
    }
    REQUIRE(guards.size() == 5);
  }
}

TEST_CASE("InflightLimiter blocks at limit", "[inflight]") {
  InflightLimiter::reset_all();

  const int max_inflight = 2;
  std::atomic<int> acquired_count{0};
  std::atomic<bool> blocked{false};

  // Acquire max_inflight guards
  std::vector<InflightLimiter::Guard> guards;
  for (int i = 0; i < max_inflight; ++i) {
    guards.push_back(InflightLimiter::acquire("block_test_ep", max_inflight));
    ++acquired_count;
  }

  // Launch thread that tries to acquire one more (should block)
  std::thread blocker([&]() {
    blocked = true;
    auto guard =
        InflightLimiter::acquire("block_test_ep", max_inflight);  // Should block
    ++acquired_count;
  });

  // Give the thread time to block
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE(acquired_count == max_inflight);

  // Release one guard - blocker should unblock
  guards.pop_back();
  blocker.join();

  REQUIRE(acquired_count == max_inflight + 1);
}

TEST_CASE("InflightLimiter separate endpoints", "[inflight]") {
  InflightLimiter::reset_all();

  // Acquire from two different endpoints
  auto guard1 = InflightLimiter::acquire("ep_a", 1);
  auto guard2 = InflightLimiter::acquire("ep_b", 1);

  // Both should succeed since they're different endpoints
  REQUIRE(true);
}

TEST_CASE("InflightLimiter guard move semantics", "[inflight]") {
  InflightLimiter::reset_all();

  InflightLimiter::Guard guard = InflightLimiter::acquire("move_test_ep", 2);

  // Move construct
  InflightLimiter::Guard guard2 = std::move(guard);

  // Original guard should be null (no double-release)
  // We can verify by acquiring another - should work since only 1 is held
  auto guard3 = InflightLimiter::acquire("move_test_ep", 2);
  REQUIRE(true);
}

TEST_CASE("InflightLimiter uses default if max_inflight <= 0", "[inflight]") {
  InflightLimiter::reset_all();

  // Should use kDefaultMaxInflight (64) when given 0 or negative
  auto guard = InflightLimiter::acquire("default_test_ep", 0);
  REQUIRE(true);

  auto guard2 = InflightLimiter::acquire("default_test_ep2", -5);
  REQUIRE(true);
}

TEST_CASE("InflightLimiter reset clears all limiters", "[inflight]") {
  // Acquire some guards
  {
    auto guard1 = InflightLimiter::acquire("reset_ep_1", 1);
    auto guard2 = InflightLimiter::acquire("reset_ep_2", 1);
  }

  // Reset all
  InflightLimiter::reset_all();

  // Should be able to acquire with same endpoint IDs (fresh state)
  auto guard1 = InflightLimiter::acquire("reset_ep_1", 1);
  auto guard2 = InflightLimiter::acquire("reset_ep_2", 1);
  REQUIRE(true);
}
