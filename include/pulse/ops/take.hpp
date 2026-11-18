#pragma once
#include <atomic>
#include <memory>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// take(n): emits the first n values, then completes and unsubscribes upstream.
struct op_take {
  std::size_t n;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, n = n](auto on_next, auto on_err, auto on_done) {
      if (n == 0) {
        if (on_done) on_done();
        return subscription{};
      }
      auto left = std::make_shared<std::atomic<std::size_t>>(n);
      auto finished = std::make_shared<std::atomic<bool>>(false);
      auto composite = std::make_shared<composite_subscription>();

      subscription sub = src.subscribe(
          [left, finished, on_next, on_done, composite](const T& v) {
            if (finished->load(std::memory_order_acquire)) return;
            std::size_t rem = left->load(std::memory_order_acquire);
            do {
              if (rem == 0) return;
            } while (!left->compare_exchange_weak(rem, rem - 1, std::memory_order_acq_rel));
            on_next(v);
            if (rem == 1 && !finished->exchange(true)) {
              if (on_done) on_done();
              composite->reset();
            }
          },
          [finished, on_err, composite](std::exception_ptr e) {
            if (finished->exchange(true)) return;
            if (on_err) on_err(e);
            composite->reset();
          },
          [finished, on_done, composite] {
            if (finished->exchange(true)) return;
            if (on_done) on_done();
            composite->reset();
          });

      composite->add(std::move(sub));
      return subscription([composite] { composite->reset(); });
    });
  }
};

inline auto take(std::size_t n) { return op_take{n}; }

} // namespace pulse
