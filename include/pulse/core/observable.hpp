#pragma once
#include <functional>
#include <utility>
#include <exception>
#include <pulse/core/subscription.hpp>

namespace pulse {

template <class T>
class observable {
public:
  using value_type = T;
  using OnNext = std::function<void(const T&)>;
  using OnErr  = std::function<void(std::exception_ptr)>;
  using OnDone = std::function<void()>;

  // Factory: create observable from subscribe function
  static observable create(std::function<subscription(OnNext, OnErr, OnDone)> impl) {
    return observable(std::move(impl));
  }

  // Subscription
  subscription subscribe(OnNext on_next,
                         OnErr  on_err  = {},
                         OnDone on_done = {}) const {
    return impl_(std::move(on_next), std::move(on_err), std::move(on_done));
  }

private:
  explicit observable(std::function<subscription(OnNext, OnErr, OnDone)> impl)
    : impl_(std::move(impl)) {}

  std::function<subscription(OnNext, OnErr, OnDone)> impl_;
};

} // namespace pulse
