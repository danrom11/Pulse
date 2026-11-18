#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// src | with_latest_from(other, f): for every value of src, emits
// f(value, latest_other) - only once `other` has emitted at least once.
// Completion is driven by src only.
template <class O, class F>
struct op_with_latest_from {
  observable<O> other;
  F f;

  template <class T>
  auto operator()(const observable<T>& src) const {
    using R = std::decay_t<std::invoke_result_t<const F&, const T&, const O&>>;
    return observable<R>::create([src, other = other, f = f](auto on_next, auto on_err,
                                                             auto on_done) {
      struct state {
        std::mutex m;
        std::optional<O> latest;
        std::atomic<bool> alive{true};
      };
      auto st = std::make_shared<state>();
      auto comp = std::make_shared<composite_subscription>();
      comp->add(other.subscribe(
          [st](const O& o) {
            std::lock_guard<std::mutex> lock(st->m);
            st->latest = o;
          },
          [st, comp, on_err](std::exception_ptr e) {
            if (!st->alive.exchange(false)) return;
            if (on_err) on_err(e);
            comp->reset();
          }));
      comp->add(src.subscribe(
          [st, f, on_next](const T& v) {
            if (!st->alive) return;
            std::optional<O> o;
            {
              std::lock_guard<std::mutex> lock(st->m);
              o = st->latest;
            }
            if (o && on_next) on_next(f(v, *o));
          },
          [st, comp, on_err](std::exception_ptr e) {
            if (!st->alive.exchange(false)) return;
            if (on_err) on_err(e);
            comp->reset();
          },
          [st, comp, on_done] {
            if (!st->alive.exchange(false)) return;
            if (on_done) on_done();
            comp->reset();
          }));
      return subscription([st, comp] {
        st->alive = false;
        comp->reset();
      });
    });
  }
};

template <class O, class F>
inline auto with_latest_from(observable<O> other, F f) {
  return op_with_latest_from<O, F>{std::move(other), std::move(f)};
}

} // namespace pulse
