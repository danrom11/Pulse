#pragma once
#include <functional>
#include <utility>
#include <type_traits>

namespace pulse {

// RAII wrapper around the unsubscribe operation.
// - Copying is PROHIBITED (preventing double-cancel/UB).
// - Moving is allowed: ownership is moved, the original object is reset.
// - By default, the destructor calls cancel (can be disabled with a flag).
class subscription {
public:
  using cancel_fn = std::function<void()>;

  // Creates an empty subscription.
  subscription() noexcept = default;

  // Construct from the cancel function.
  explicit subscription(cancel_fn fn, bool cancel_on_dtor = true) noexcept
    : cancel_(std::move(fn)), cancel_on_dtor_(cancel_on_dtor) {}

  // Copying is prohibited: one subscription - one owner.
  subscription(const subscription&) = delete;
  subscription& operator=(const subscription&) = delete;

  // Move: move the cancel right.
  subscription(subscription&& other) noexcept
    : cancel_(std::move(other.cancel_))
    , cancel_on_dtor_(other.cancel_on_dtor_) {
    other.cancel_ = nullptr;
    // move the flag to the "idle" state so that a random dtor doesn't cancel it again
    other.cancel_on_dtor_ = false;
  }

  subscription& operator=(subscription&& other) noexcept {
    if (this != &other) {
      reset();
      cancel_ = std::move(other.cancel_);
      cancel_on_dtor_ = other.cancel_on_dtor_;
      other.cancel_ = nullptr;
      other.cancel_on_dtor_ = false;
    }
    return *this;
  }

  // By default - cancel on destruction (if enabled).
  ~subscription() { reset(); }

  // Unsubscribes once. Repeated calls are no-op.
  void reset() noexcept {
    if (cancel_) {
      try {
        cancel_(); // user cancellation can throw - suppress exceptions
      } catch (...) {
        //TODO:
        // By default we swallow so that the destructor does not throw. 
        // If you need telemetry, you can add a hook/logger here.
      }
      cancel_ = nullptr;
    }
    // after reset() the autocancel in the destructor is no longer needed
    cancel_on_dtor_ = false;
  }

  // Explicitly "forget" the cancellation, allowing the thread to continue.
  // Useful if responsibility for cancellation has moved to another object.
  void release() noexcept {
    cancel_ = nullptr;
    cancel_on_dtor_ = false;
  }

  // Validity indicator (there is an active cancellation).
  explicit operator bool() const noexcept { return static_cast<bool>(cancel_); }

  // Swap places (but exception-safe).
  void swap(subscription& other) noexcept {
    using std::swap;
    swap(cancel_, other.cancel_);
    swap(cancel_on_dtor_, other.cancel_on_dtor_);
  }

  // Set policy: whether to cancel in destructor.
  subscription& cancel_on_destruct(bool v) noexcept {
    cancel_on_dtor_ = v && static_cast<bool>(cancel_);
    return *this;
  }

private:
  cancel_fn cancel_{};
  bool cancel_on_dtor_{true};
};

// Utility: make a subscription from a callable object (lambda, functor, ptr function).
template <class F,
          std::enable_if_t<std::is_invocable_v<F&>, int> = 0>
inline subscription make_subscription(F&& f, bool cancel_on_dtor = true) {
  // Wrap it in std::function<void()>
  return subscription(subscription::cancel_fn(std::forward<F>(f)), cancel_on_dtor);
}

// Utility: "empty" subscription (no-op).
inline subscription empty_subscription() noexcept {
  return subscription{};
}

} // namespace pulse
