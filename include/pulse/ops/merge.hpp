#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <atomic>
#include <memory>
#include <utility>

namespace pulse {

// merge(a, b): Concurrent merge of two observable<T>
template <class T>
inline observable<T> merge(const observable<T>& a, const observable<T>& b) {
  return observable<T>::create([a, b](auto on_next, auto on_error, auto on_completed){
    struct state {
      std::atomic<bool> alive{true};        // is downstream alive
      std::atomic<bool> terminated{false};  // have on_error/on_completed already been sent
      std::atomic<int> remaining{2};        // how many upstreams have not completed
      subscription up1;
      subscription up2;
    };
    auto st = std::make_shared<state>();
    auto wst = std::weak_ptr<state>(st);

    auto try_complete = [wst, on_completed]{
      if (auto s = wst.lock()) {
        if (!s->alive) return;
        if (s->remaining.load() == 0) {
          // protection against double completion
          bool expected = false;
          if (s->terminated.compare_exchange_strong(expected, true)) {
            s->alive = false;
            s->up1.reset();
            s->up2.reset();
            if (on_completed) on_completed();
          }
        }
      }
    };

    // subscribe to A
    st->up1 = a.subscribe(
      // on_next
      [wst, on_next](const T& v){
        if (auto s = wst.lock()) {
          if (!s->alive) return;
          if (on_next) on_next(v);
        }
      },
      // on_error
      [wst, on_error](std::exception_ptr e){
        if (auto s = wst.lock()) {
          if (!s->alive) return;
          bool expected = false;
          if (s->terminated.compare_exchange_strong(expected, true)) {
            s->alive = false;
            s->up2.reset();
            s->up1.reset();
            if (on_error) on_error(e);
          }
        }
      },
      // on_completed
      [try_complete, wst]{
        if (auto s = wst.lock()) {
          if (!s->alive) return;
          s->remaining.fetch_sub(1);
          try_complete();
        }
      }
    );

    // subscribe to B
    st->up2 = b.subscribe(
      [wst, on_next](const T& v){
        if (auto s = wst.lock()) {
          if (!s->alive) return;
          if (on_next) on_next(v);
        }
      },
      [wst, on_error](std::exception_ptr e){
        if (auto s = wst.lock()) {
          if (!s->alive) return;
          bool expected = false;
          if (s->terminated.compare_exchange_strong(expected, true)) {
            s->alive = false;
            s->up1.reset();
            s->up2.reset();
            if (on_error) on_error(e);
          }
        }
      },
      [try_complete, wst]{
        if (auto s = wst.lock()) {
          if (!s->alive) return;
          s->remaining.fetch_sub(1);
          try_complete();
        }
      }
    );

    // unsubscribe downstream
    return subscription([st]{
      if (!st) return;
      st->alive = false;
      st->up1.reset();
      st->up2.reset();
    });
  });
}

// merge(a, b, c, ...): variadic - reduce to paired merges
template <class T, class... Rest>
inline observable<T> merge(const observable<T>& a, const observable<T>& b, const Rest&... rest) {
  if constexpr (sizeof...(rest) == 0) {
    return merge<T>(a, b);
  } else {
    return merge<T>(merge<T>(a, b), rest...);
  }
}

} // namespace pulse
