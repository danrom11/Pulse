#pragma once
#include <exception>
#include <functional>
#include <utility>
#include <pulse/core/subscription.hpp>

namespace pulse {

// Unit type for value-less streams (signal<>, timer ticks, ...).
struct unit {
  friend constexpr bool operator==(unit, unit) noexcept { return true; }
  friend constexpr bool operator!=(unit, unit) noexcept { return false; }
};

template <class T>
class observable {
public:
  using value_type = T;
  using OnNext = std::function<void(const T&)>;
  using OnErr = std::function<void(std::exception_ptr)>;
  using OnDone = std::function<void()>;
  using subscribe_fn = std::function<subscription(OnNext, OnErr, OnDone)>;

  // A default-constructed observable never emits anything.
  observable() = default;

  // Factory: create an observable from a subscribe function.
  static observable create(subscribe_fn impl) { return observable(std::move(impl)); }

  subscription subscribe(OnNext on_next, OnErr on_err = {}, OnDone on_done = {}) const {
    if (!impl_) return subscription{};
    return impl_(std::move(on_next), std::move(on_err), std::move(on_done));
  }

  // true if this observable has a source (i.e. not default constructed).
  bool valid() const noexcept { return static_cast<bool>(impl_); }

private:
  explicit observable(subscribe_fn impl) : impl_(std::move(impl)) {}
  subscribe_fn impl_;
};

} // namespace pulse
