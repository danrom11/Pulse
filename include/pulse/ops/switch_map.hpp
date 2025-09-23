#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <memory>
#include <utility>

namespace pulse {

// switch_map: for each value T from the outer observable
// constructs an observable<U>, cancels the previous inner one, and subscribes to the new one
template <class F>
struct op_switch_map {
  F f; // F: T -> observable<U>

  template <class T>
  auto operator()(const observable<T>& src) const {
    using InnerObs = decltype(f(std::declval<const T&>()));
    using U = typename InnerObs::value_type;   // get the element type from observable<U>

    return observable<U>::create([src, f = f](auto on_next, auto on_err, auto on_done) {
      auto current = std::make_shared<subscription>(); // stores the current inner subscriber

      return src.subscribe(
        // on_next outer
        [f, current, on_next, on_err, on_done](const T& v) {
          // cancel the previous inner subscription
          current->reset();
          // create a new inner observable
          auto inner = f(v);
          // subscribe and save
          *current = inner.subscribe(on_next, on_err, on_done);
        },
        // on_error outer
        on_err,
        // on_done outer
        on_done
      );
    });
  }
};

// helper for convenient calling: switch_map([](T){ return observable<U>; })
template <class F>
inline auto switch_map(F f) {
  return op_switch_map<F>{ std::move(f) };
}

} // namespace pulse
