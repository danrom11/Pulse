#pragma once
#include <pulse/core/observable.hpp>
#include <utility>

namespace pulse {

template <class V>
struct op_start_with {
  V seed;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, seed = seed](auto on_next, auto on_err, auto on_done){
      if (on_next) on_next(seed);
      return src.subscribe(on_next, on_err, on_done);
    });
  }
};
template <class V>
inline auto start_with(V v){ return op_start_with<V>{ std::move(v) }; }

} // namespace pulse
