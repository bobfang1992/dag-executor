#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <unordered_map>

namespace rankd {

// Default max inflight if not specified in endpoint policy
constexpr int kDefaultMaxInflight = 64;

/**
 * InflightLimiter - global per-endpoint concurrency limiter.
 *
 * Uses counting semaphores to limit the number of concurrent Redis
 * operations per endpoint. This is shared across all requests to
 * prevent overwhelming Redis with too many concurrent commands.
 *
 * Thread-safe: Uses mutex for semaphore map access, semaphores are
 * inherently thread-safe.
 *
 * Usage:
 *   auto guard = InflightLimiter::acquire("ep_0001", max_inflight);
 *   // ... do Redis operation ...
 *   // guard destructor releases the semaphore
 */
class InflightLimiter {
public:
  // RAII guard that releases the semaphore on destruction
  class Guard {
  public:
    Guard(std::counting_semaphore<>* sem, std::atomic<int>* counter)
        : sem_(sem), counter_(counter) {}
    ~Guard() {
      if (sem_) {
        if (counter_) {
          counter_->fetch_sub(1, std::memory_order_relaxed);
        }
        sem_->release();
      }
    }

    // Non-copyable, movable
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&& other) noexcept
        : sem_(other.sem_), counter_(other.counter_) {
      other.sem_ = nullptr;
      other.counter_ = nullptr;
    }
    Guard& operator=(Guard&& other) noexcept {
      if (this != &other) {
        if (sem_) {
          if (counter_) {
            counter_->fetch_sub(1, std::memory_order_relaxed);
          }
          sem_->release();
        }
        sem_ = other.sem_;
        counter_ = other.counter_;
        other.sem_ = nullptr;
        other.counter_ = nullptr;
      }
      return *this;
    }

  private:
    std::counting_semaphore<>* sem_;
    std::atomic<int>* counter_;
  };

  // Acquire a slot for the given endpoint.
  // Blocks if max_inflight slots are already in use.
  // Returns a Guard that releases the slot on destruction.
  static Guard acquire(const std::string& endpoint_id, int max_inflight);

  // Get current inflight count for an endpoint (for testing/metrics)
  static int get_inflight_count(const std::string& endpoint_id);

  // Reset all limiters (for testing)
  static void reset_all();

private:
  struct EndpointState {
    std::unique_ptr<std::counting_semaphore<>> semaphore;
    int max_inflight;
    std::atomic<int> current_inflight{0};
  };

  static std::mutex mutex_;
  // Use unique_ptr because EndpointState contains std::atomic (non-movable)
  static std::unordered_map<std::string, std::unique_ptr<EndpointState>> limiters_;
};

}  // namespace rankd
