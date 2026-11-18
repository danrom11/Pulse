#pragma once
#include <mutex>
#include <vector>
#include <pulse/core/subscription.hpp>

namespace pulse {

// A bag of subscriptions that are cancelled together.
// After reset() the bag is "closed": anything added later is cancelled
// immediately. Use clear() to cancel everything but keep accepting new ones.
class composite_subscription {
public:
  composite_subscription() = default;

  void add(subscription s) {
    {
      std::lock_guard<std::mutex> lock(m_);
      if (!cancelled_) {
        subs_.push_back(std::move(s));
        return;
      }
    }
    s.reset();
  }

  composite_subscription& operator+=(subscription s) {
    add(std::move(s));
    return *this;
  }

  // Cancel all and close the bag.
  void reset() {
    std::vector<subscription> local;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (cancelled_) return;
      cancelled_ = true;
      local.swap(subs_);
    }
    for (auto& s : local) s.reset();
  }

  // Cancel all, keep the bag open.
  void clear() {
    std::vector<subscription> local;
    {
      std::lock_guard<std::mutex> lock(m_);
      local.swap(subs_);
    }
    for (auto& s : local) s.reset();
  }

  bool is_cancelled() const {
    std::lock_guard<std::mutex> lock(m_);
    return cancelled_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(m_);
    return subs_.size();
  }

  ~composite_subscription() { reset(); }

  composite_subscription(const composite_subscription&) = delete;
  composite_subscription& operator=(const composite_subscription&) = delete;
  composite_subscription(composite_subscription&&) = delete;
  composite_subscription& operator=(composite_subscription&&) = delete;

private:
  mutable std::mutex m_;
  bool cancelled_{false};
  std::vector<subscription> subs_;
};

// Shorter name for the common "owner keeps a set of subscriptions" use.
using subscription_bag = composite_subscription;

} // namespace pulse
