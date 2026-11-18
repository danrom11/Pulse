#pragma once
#include <pulse/core/backpressure.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/topic.hpp>

namespace pulse {

// Turn topic<T> into observable<T>, delivering on the given executor.
// The topic must outlive subscriptions created through the observable only
// for as long as it should publish; dangling subscriptions are harmless.
template <class T>
inline observable<T> as_observable(topic<T>& t, executor& ex) {
  return observable<T>::create([&t, &ex](auto on_next, auto, auto) {
    return t.subscribe(ex, priority{0}, bp_none{}, [on_next](const T& v) { on_next(v); });
  });
}

// Synchronous delivery on the publishing thread.
template <class T>
inline observable<T> as_observable(topic<T>& t) {
  return as_observable(t, inline_exec());
}

} // namespace pulse
