#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// throttle_latest(win, ex): leading + trailing-latest per window.
// - the first value of a burst is emitted immediately (caller's thread);
// - the newest value seen during the window is emitted on `ex` when the
//   window ends, which starts a new window.
template <class Rep, class Period>
struct throttle_latest_op {
  std::chrono::nanoseconds win;
  executor* exec;

  template <class T>
  observable<T> operator()(const observable<T>& src) const {
    return observable<T>::create([src, win = win, execp = exec](auto on_next, auto on_error,
                                                                 auto on_completed) {
      struct state_t {
        std::mutex m;
        bool closed{false};
        std::optional<T> pending;
        std::atomic<bool> alive{true};
        subscription timer;
        subscription upstream;
      };
      auto st = std::make_shared<state_t>();
      std::weak_ptr<state_t> w = st;

      // Window end: emit pending (and open a new window) or reopen the gate.
      auto open_window = std::make_shared<std::function<void()>>();
      std::weak_ptr<std::function<void()>> wopen = open_window;
      *open_window = [w, win, execp, on_next, wopen] {
        auto s = w.lock();
        if (!s || !s->alive) return;
        auto t = timer_service::instance().schedule_after(win, [w, execp, on_next, wopen] {
          auto s2 = w.lock();
          if (!s2 || !s2->alive) return;
          std::optional<T> out;
          {
            std::lock_guard<std::mutex> lk(s2->m);
            if (s2->pending) {
              out = std::move(s2->pending);
              s2->pending.reset();
            } else {
              s2->closed = false;
            }
          }
          if (!out) return;
          execp->post([w, on_next, val = std::move(*out)] {
            auto s3 = w.lock();
            if (s3 && s3->alive && on_next) on_next(val);
          });
          if (auto again = wopen.lock()) (*again)(); // trailing emit starts a new window
        });
        std::lock_guard<std::mutex> lk(s->m);
        s->timer = std::move(t);
      };

      st->upstream = src.subscribe(
          [w, on_next, open_window](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            bool leading = false;
            {
              std::lock_guard<std::mutex> lk(s->m);
              if (!s->closed) {
                s->closed = true;
                leading = true;
              } else {
                s->pending = v;
              }
            }
            if (leading) {
              (*open_window)();
              if (on_next) on_next(v);
            }
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

      return subscription([st, open_window] {
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
auto throttle_latest(std::chrono::duration<Rep, Period> win, executor& exec) {
  return throttle_latest_op<Rep, Period>{std::chrono::duration_cast<std::chrono::nanoseconds>(win),
                                         &exec};
}

} // namespace pulse
