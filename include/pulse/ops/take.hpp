#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/composite_subscription.hpp>
#include <atomic>

namespace pulse {

struct op_take {
  std::size_t n;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, n = n](auto on_next, auto on_err, auto on_done){
      if (n == 0) {
        if (on_done) on_done();
        return subscription{};
      }
      auto left = std::make_shared<std::atomic<std::size_t>>(n);
      auto composite = std::make_shared<composite_subscription>();

      subscription sub = src.subscribe(
        [left, on_next, on_done, composite](const T& v){
          auto rem = left->fetch_sub(1, std::memory_order_acq_rel);
          if (rem == 0) return;
          on_next(v);
          if (rem == 1) {
            if (on_done) on_done();
            composite->reset();
          }
        },
        [on_err, composite](std::exception_ptr e){
          if (on_err) on_err(e);
          composite->reset();
        },
        [on_done]{
          if (on_done) on_done();
        }
      );

      composite->add(std::move(sub));
      return subscription([composite]{ composite->reset(); });
    });
  }
};

inline auto take(std::size_t n){ return op_take{n}; }

} // namespace pulse
