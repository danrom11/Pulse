#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

#include <functional>
#include <memory>
#include <deque>
#include <atomic>
#include <exception>
#include <utility>
#include <type_traits>

namespace pulse {

template <class Fn>
struct op_concat_map {
  Fn fn;

  template <class T>
  auto operator()(const observable<T>& src) const {
    using inner_observable = decltype(fn(std::declval<T>()));
    using U = typename inner_observable::value_type;

    return observable<U>::create([src, fn = fn](auto on_next, auto on_error, auto on_completed) {
      struct state {
        std::atomic<bool> alive{true};
        bool outer_completed = false;
        bool inner_active = false;

        std::deque<inner_observable> queue;
        subscription sub_up;
        subscription sub_in;

        std::function<void()> drain;
      };

      auto st = std::make_shared<state>();
      auto wst = std::weak_ptr<state>(st);

      st->drain = [wst, on_next, on_error, on_completed]() mutable {
        auto s = wst.lock();
        if (!s || !s->alive) return;
        if (s->inner_active) return;

        // if there are no internal ones and the upstream is complete, terminate the downstream
        if (s->queue.empty()) {
          if (s->outer_completed) {
            s->alive = false;
            if (on_completed) on_completed();
          }
          return;
        }

        auto inner = std::move(s->queue.front());
        s->queue.pop_front();
        s->inner_active = true;

        s->sub_in = inner.subscribe(
          // on_next
          [wst, on_next](const U& v){
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              if (on_next) on_next(v);
            }
          },
          // on_error
          [wst, on_error](std::exception_ptr e){
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              s2->alive = false;
              s2->queue.clear();
              s2->sub_up.reset();
              s2->sub_in.reset();
              if (on_error) on_error(e);
            }
          },
          // on_completed
          [wst]{
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              s2->sub_in.reset();
              s2->inner_active = false;
              s2->drain();
            }
          }
        );
      };

      st->sub_up = src.subscribe(
        // outer on_next -> generate inner and put in queue
        [st, on_error, fn](const T& v){
          if (!st->alive) return;
          try {
            st->queue.push_back(fn(v));
          } catch (...) {
            auto e = std::current_exception();
            st->alive = false;
            st->queue.clear();
            st->sub_in.reset();
            st->sub_up.reset();
            if (on_error) on_error(e);
            return;
          }
          st->drain();
        },
        // outer on_error
        [st, on_error](std::exception_ptr e){
          if (!st->alive) return;
          st->alive = false;
          st->queue.clear();
          st->sub_in.reset();
          st->sub_up.reset();
          if (on_error) on_error(e);
        },
        // outer on_completed
        [st]{
          if (!st->alive) return;
          st->outer_completed = true;
          st->drain();
        }
      );

      return subscription([st]{
        if (!st) return;
        st->alive = false;
        st->queue.clear();
        st->sub_in.reset();
        st->sub_up.reset();
      });
    });
  }
};

template <class Fn>
inline auto concat_map(Fn fn) {
  return op_concat_map<std::decay_t<Fn>>{ std::forward<Fn>(fn) };
}

} // namespace pulse
