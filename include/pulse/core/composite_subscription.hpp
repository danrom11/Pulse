#pragma once
#include <vector>
#include <mutex>
#include <pulse/core/subscription.hpp>

namespace pulse {

class composite_subscription {
public:
  composite_subscription() = default;

  void add(subscription s) {
    std::lock_guard<std::mutex> lock(m_);
    if (cancelled_) {
      s.reset();
      return;
    }
    subs_.push_back(std::move(s));
  }

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

  ~composite_subscription() { reset(); }

  composite_subscription(const composite_subscription&)            = delete;
  composite_subscription& operator=(const composite_subscription&) = delete;

  composite_subscription(composite_subscription&&)            = delete;
  composite_subscription& operator=(composite_subscription&&) = delete;

private:
  std::mutex m_;
  bool cancelled_{false};
  std::vector<subscription> subs_;
};

} // namespace pulse
