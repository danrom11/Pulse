#pragma once
#include <pulse/core/topic.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/backpressure.hpp>

namespace pulse {

// Simple adapter: turn topic<T> into observable<T> by subscribing to the specified executor
template <class T>
inline observable<T> as_observable(topic<T>& t, executor& ex) {
  return observable<T>::create([&t, &ex](auto on_next, auto, auto){
    return t.subscribe(ex, priority{0}, bp_none{}, [on_next](const T& v){ on_next(v); });
  });
}

} // namespace pulse
