#pragma once
#include <pulse/core/observable.hpp>
#include <utility>

namespace pulse {

template <class Pred>
struct op_filter {
  Pred p;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, p = p](auto on_next, auto on_err, auto on_done){
      return src.subscribe(
        [p, on_next](const T& v){ if (p(v)) on_next(v); },
        on_err, on_done
      );
    });
  }
};
template <class Pred> op_filter(Pred)->op_filter<Pred>;
template <class Pred> inline auto filter(Pred p){ return op_filter<Pred>{ std::move(p) }; }

} // namespace pulse
