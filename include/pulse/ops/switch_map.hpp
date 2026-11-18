#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// switch_map(f): for each outer value, subscribe to f(value) and cancel the
// previous inner observable. Completes when the outer and the current inner
// have both completed. Unsubscribing cancels the outer and the inner.
template <class F>
struct op_switch_map {
  F f; // F: T -> observable<U>

  template <class T>
  auto operator()(const observable<T>& src) const {
    using InnerObs = std::decay_t<decltype(f(std::declval<const T&>()))>;
    using U = typename InnerObs::value_type;

    return observable<U>::create([src, f = f](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::atomic<bool> alive{true};
        std::uint64_t gen{0};
        bool outer_done{false};
        bool inner_active{false};
        subscription inner;
        subscription outer;
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;

      auto fail = [w, on_err](std::exception_ptr e) {
        auto s = w.lock();
        if (!s || !s->alive.exchange(false)) return;
        subscription a, b;
        {
          std::lock_guard<std::mutex> lock(s->m);
          a = std::move(s->inner);
          b = std::move(s->outer);
        }
        a.reset();
        b.reset();
        if (on_err) on_err(e);
      };

      auto outer_sub = src.subscribe(
          [w, f, on_next, on_done, fail](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            std::uint64_t my;
            subscription old;
            {
              std::lock_guard<std::mutex> lock(s->m);
              my = ++s->gen;
              old = std::move(s->inner);
              s->inner_active = true;
            }
            old.reset();

            InnerObs inner;
            try {
              inner = f(v);
            } catch (...) {
              fail(std::current_exception());
              return;
            }

            auto sub = inner.subscribe(
                [w, my, on_next](const U& u) {
                  auto s2 = w.lock();
                  if (!s2 || !s2->alive) return;
                  {
                    std::lock_guard<std::mutex> lock(s2->m);
                    if (s2->gen != my) return;
                  }
                  if (on_next) on_next(u);
                },
                [w, my, fail](std::exception_ptr e) {
                  auto s2 = w.lock();
                  if (!s2) return;
                  {
                    std::lock_guard<std::mutex> lock(s2->m);
                    if (s2->gen != my) return;
                  }
                  fail(e);
                },
                [w, my, on_done] {
                  auto s2 = w.lock();
                  if (!s2 || !s2->alive) return;
                  bool finish = false;
                  {
                    std::lock_guard<std::mutex> lock(s2->m);
                    if (s2->gen != my) return;
                    s2->inner_active = false;
                    finish = s2->outer_done;
                  }
                  if (finish && s2->alive.exchange(false) && on_done) on_done();
                });

            std::lock_guard<std::mutex> lock(s->m);
            if (s->gen == my && s->alive) s->inner = std::move(sub);
          },
          fail,
          [w, on_done] {
            auto s = w.lock();
            if (!s || !s->alive) return;
            bool finish = false;
            {
              std::lock_guard<std::mutex> lock(s->m);
              s->outer_done = true;
              finish = !s->inner_active;
            }
            if (finish && s->alive.exchange(false) && on_done) on_done();
          });

      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->alive) st->outer = std::move(outer_sub);
      }
      outer_sub.reset();

      return subscription([st] {
        st->alive = false;
        subscription a, b;
        {
          std::lock_guard<std::mutex> lock(st->m);
          ++st->gen;
          a = std::move(st->inner);
          b = std::move(st->outer);
        }
        a.reset();
        b.reset();
      });
    });
  }
};

template <class F>
inline auto switch_map(F f) {
  return op_switch_map<F>{std::move(f)};
}

} // namespace pulse
