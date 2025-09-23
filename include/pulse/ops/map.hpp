#pragma once
#include <pulse/core/observable.hpp>
#include <type_traits>
#include <utility>

namespace pulse {

template <class F>
struct op_map {
  F f;
  template <class T>
  auto operator()(const observable<T>& src) const {
    using U = std::invoke_result_t<F, const T&>;
    return observable<U>::create([src, f = f](auto on_next, auto on_err, auto on_done){
      return src.subscribe(
        [f, on_next](const T& v){ on_next(f(v)); },
        on_err, on_done
      );
    });
  }
};
template <class F> op_map(F)->op_map<F>;
template <class F> inline auto map(F f){ return op_map<F>{ std::move(f) }; }

} // namespace pulse
