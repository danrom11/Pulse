#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// catch_error(handler): on error, switch to the observable returned by
// handler(exception_ptr). Values emitted before the error are kept.
template <class H>
struct op_catch_error {
  H handler;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, h = handler](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        subscription current;
        bool switched{false};
        std::atomic<bool> alive{true};
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;
      auto up = src.subscribe(
          [w, on_next](const T& v) {
            auto s = w.lock();
            if (s && s->alive && on_next) on_next(v);
          },
          [w, h, on_next, on_err, on_done](std::exception_ptr e) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            observable<T> fallback;
            try {
              fallback = h(e);
            } catch (...) {
              if (on_err) on_err(std::current_exception());
              return;
            }
            auto sub = fallback.subscribe(on_next, on_err, on_done);
            subscription old;
            {
              std::lock_guard<std::mutex> lock(s->m);
              s->switched = true;
              old = std::move(s->current);
              s->current = std::move(sub);
            }
            old.reset(); // detach from the failed upstream (idempotent)
          },
          on_done);
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (!st->switched) st->current = std::move(up);
      }
      return subscription([st] {
        st->alive = false;
        subscription c;
        {
          std::lock_guard<std::mutex> lock(st->m);
          c = std::move(st->current);
        }
        c.reset();
      });
    });
  }
};
template <class H>
inline auto catch_error(H h) {
  return op_catch_error<H>{std::move(h)};
}

// on_error_return(value): on error, emit `value` and complete.
template <class V>
struct op_on_error_return {
  V value;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, v = value](auto on_next, auto, auto on_done) {
      return src.subscribe(
          on_next,
          [v, on_next, on_done](std::exception_ptr) {
            if (on_next) on_next(T(v));
            if (on_done) on_done();
          },
          on_done);
    });
  }
};
template <class V>
inline auto on_error_return(V v) {
  return op_on_error_return<V>{std::move(v)};
}

// default_if_empty(value): emits `value` if the source completes without values.
template <class V>
struct op_default_if_empty {
  V value;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, v = value](auto on_next, auto on_err, auto on_done) {
      auto any = std::make_shared<std::atomic<bool>>(false);
      return src.subscribe(
          [any, on_next](const T& x) {
            any->store(true);
            if (on_next) on_next(x);
          },
          on_err,
          [any, v, on_next, on_done] {
            if (!any->load() && on_next) on_next(T(v));
            if (on_done) on_done();
          });
    });
  }
};
template <class V>
inline auto default_if_empty(V v) {
  return op_default_if_empty<V>{std::move(v)};
}

} // namespace pulse
