#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// throttle(win, ex): emits the first value of a burst immediately (on the
// caller's thread), then ignores values until `win` has passed.
// `ex` is kept for API compatibility; the window timer does not block it.
template <class Rep, class Period>
struct throttle_op {
  std::chrono::nanoseconds win;
  executor* exec;

  template <class T>
  observable<T> operator()(const observable<T>& src) const {
    return observable<T>::create([src, win = win](auto on_next, auto on_error, auto on_completed) {
      struct state_t {
        std::mutex m;
        bool closed{false};
        std::atomic<bool> alive{true};
        subscription timer;
        subscription upstream;
      };
      auto st = std::make_shared<state_t>();
      std::weak_ptr<state_t> w = st;

      st->upstream = src.subscribe(
          [w, win, on_next](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive.load(std::memory_order_acquire)) return;
            {
              std::lock_guard<std::mutex> lk(s->m);
              if (s->closed) return;
              s->closed = true;
            }
            auto t = timer_service::instance().schedule_after(win, [w] {
              if (auto s2 = w.lock()) {
                std::lock_guard<std::mutex> lk(s2->m);
                s2->closed = false;
              }
            });
            {
              std::lock_guard<std::mutex> lk(s->m);
              s->timer = std::move(t);
            }
            if (on_next) on_next(v); // leading edge, outside the lock
          },
          [w, on_error](std::exception_ptr e) {
            auto s = w.lock();
            if (!s || !s->alive.exchange(false)) return;
            if (on_error) on_error(std::move(e));
          },
          [w, on_completed] {
            auto s = w.lock();
            if (!s || !s->alive.exchange(false)) return;
            if (on_completed) on_completed();
          });

      return subscription([st] {
        st->alive = false;
        subscription t;
        {
          std::lock_guard<std::mutex> lk(st->m);
          t = std::move(st->timer);
        }
        t.reset();
        st->upstream.reset();
      });
    });
  }
};

template <class Rep, class Period>
auto throttle(std::chrono::duration<Rep, Period> win, executor& exec) {
  return throttle_op<Rep, Period>{std::chrono::duration_cast<std::chrono::nanoseconds>(win), &exec};
}

template <class Rep, class Period>
auto throttle(std::chrono::duration<Rep, Period> win) {
  return throttle_op<Rep, Period>{std::chrono::duration_cast<std::chrono::nanoseconds>(win),
                                  &inline_exec()};
}

} // namespace pulse
