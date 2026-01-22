#include "inflight_limiter.h"

namespace rankd {

std::mutex InflightLimiter::mutex_;
std::unordered_map<std::string, std::unique_ptr<InflightLimiter::EndpointState>>
    InflightLimiter::limiters_;

InflightLimiter::Guard InflightLimiter::acquire(const std::string& endpoint_id,
                                                 int max_inflight) {
  if (max_inflight <= 0) {
    max_inflight = kDefaultMaxInflight;
  }

  std::counting_semaphore<>* sem = nullptr;
  std::atomic<int>* counter = nullptr;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limiters_.find(endpoint_id);
    if (it == limiters_.end()) {
      // Create new limiter for this endpoint
      auto state = std::make_unique<EndpointState>();
      state->max_inflight = max_inflight;
      state->semaphore =
          std::make_unique<std::counting_semaphore<>>(max_inflight);
      auto [inserted_it, _] =
          limiters_.emplace(endpoint_id, std::move(state));
      it = inserted_it;
    }
    sem = it->second->semaphore.get();
    counter = &it->second->current_inflight;
  }

  // Acquire semaphore (may block if at limit)
  sem->acquire();

  // Increment inflight counter after successful acquire
  counter->fetch_add(1, std::memory_order_relaxed);

  return Guard(sem, counter);
}

int InflightLimiter::get_inflight_count(const std::string& endpoint_id) {
  // Note: This is approximate - semaphores don't expose current count
  // For accurate metrics, we'd need additional tracking
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = limiters_.find(endpoint_id);
  if (it == limiters_.end()) {
    return 0;
  }
  return it->second->current_inflight.load(std::memory_order_relaxed);
}

void InflightLimiter::reset_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  limiters_.clear();
}

}  // namespace rankd
