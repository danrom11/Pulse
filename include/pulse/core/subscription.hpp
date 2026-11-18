#pragma once
#include <functional>
#include <type_traits>
#include <utility>

namespace pulse {

// RAII wrapper around an unsubscribe operation.
// - Copying is prohibited (one subscription - one owner).
// - Moving transfers ownership; the source becomes empty.
// - The destructor cancels unless cancel_on_destruct(false) / release() was used.
class subscription {
public:
  using cancel_fn = std::function<void()>;

  subscription() noexcept = default;

  explicit subscription(cancel_fn fn, bool cancel_on_dtor = true) noexcept
      : cancel_(std::move(fn)), cancel_on_dtor_(cancel_on_dtor) {}

  subscription(const subscription&) = delete;
  subscription& operator=(const subscription&) = delete;

  subscription(subscription&& other) noexcept
      : cancel_(std::move(other.cancel_)), cancel_on_dtor_(other.cancel_on_dtor_) {
    other.cancel_ = nullptr;
    other.cancel_on_dtor_ = true;
  }

  subscription& operator=(subscription&& other) noexcept {
    if (this != &other) {
      reset();
      cancel_ = std::move(other.cancel_);
      cancel_on_dtor_ = other.cancel_on_dtor_;
      other.cancel_ = nullptr;
      other.cancel_on_dtor_ = true;
    }
    return *this;
  }

  ~subscription() {
    if (cancel_on_dtor_) reset();
  }

  // Unsubscribes once. Repeated calls are no-op. Exceptions from the cancel
  // function are swallowed (reset() is used from destructors).
  void reset() noexcept {
    if (cancel_) {
      auto fn = std::move(cancel_);
      cancel_ = nullptr;
      try {
        fn();
      } catch (...) {
      }
    }
  }

  // Alias with a more explicit name.
  void unsubscribe() noexcept { reset(); }

  // Forget the cancellation: the underlying stream keeps running.
  void release() noexcept { cancel_ = nullptr; }

  // Hand the cancel function out (e.g. to store it in a container of functions).
  cancel_fn take() noexcept {
    auto fn = std::move(cancel_);
    cancel_ = nullptr;
    return fn;
  }

  bool active() const noexcept { return static_cast<bool>(cancel_); }
  explicit operator bool() const noexcept { return active(); }

  void swap(subscription& other) noexcept {
    using std::swap;
    swap(cancel_, other.cancel_);
    swap(cancel_on_dtor_, other.cancel_on_dtor_);
  }

  // Whether the destructor cancels. Detaching (false) means the stream lives
  // until it completes or until reset() is called explicitly.
  subscription& cancel_on_destruct(bool v) noexcept {
    cancel_on_dtor_ = v;
    return *this;
  }

private:
  cancel_fn cancel_{};
  bool cancel_on_dtor_{true};
};

template <class F, std::enable_if_t<std::is_invocable_v<F&>, int> = 0>
inline subscription make_subscription(F&& f, bool cancel_on_dtor = true) {
  return subscription(subscription::cancel_fn(std::forward<F>(f)), cancel_on_dtor);
}

inline subscription empty_subscription() noexcept { return subscription{}; }

} // namespace pulse
