#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <chrono>
#include <atomic>
#include <optional>
#include <mutex>
#include <thread>

namespace pulse {

// leading + trailing latest за окно
template <class Rep, class Period>
struct throttle_latest_op {
  std::chrono::nanoseconds win;
  executor* exec;

  template <class T>
  observable<T> operator()(const observable<T>& src) const {
    return observable<T>::create(
      [src, win = win, execp = exec](auto on_next, auto on_error, auto on_completed)
    {
      struct state_t {
        std::mutex m;
        bool closed{false};
        std::optional<T> pending;
        std::atomic<bool> alive{true};
      };
      auto st = std::make_shared<state_t>();

      // Schedules the end window tick:
      // - if there's a pending event, emit it now and OPEN the shutter after another window;
      // - if there's no pending event, simply open the shutter when the window ends.
      auto schedule_tick = [st, win, execp, on_next]() {
        execp->post([st, win, on_next, execp]{
          std::this_thread::sleep_for(win);

          std::optional<T> to_emit;
          {
            std::lock_guard lk(st->m);
            if (!st->alive.load(std::memory_order_acquire)) return;
            if (st->pending.has_value()) {
              to_emit = std::move(st->pending); // trailing emit
              st->pending.reset();
              // remain closed - open the shutter after the additional window below
            } else {
              st->closed = false; // open immediately if there is nothing to emit
            }
          }

          if (to_emit.has_value()) {
            if (on_next) on_next(*to_emit);
            // Let's open the shutter after another window
            execp->post([st, win]{
              std::this_thread::sleep_for(win);
              std::lock_guard lk(st->m);
              if (!st->alive.load(std::memory_order_acquire)) return;
              st->closed = false;
            });
          }
        });
      };

      return src.subscribe(
        // on_next
        [st, on_next, schedule_tick](const T& v){
          std::lock_guard lk(st->m);
          if (!st->alive.load(std::memory_order_acquire)) return;

          if (!st->closed) {
            // leading
            st->closed = true;
            if (on_next) on_next(v);
            schedule_tick();  // window start
          } else {
            // the window is moving - we remember the newest one for trailing
            st->pending = v;
          }
        },
        // on_error
        [st, on_error](std::exception_ptr e){
          {
            std::lock_guard lk(st->m);
            st->alive.store(false, std::memory_order_release);
          }
          if (on_error) on_error(std::move(e));
        },
        // on_completed
        [st, on_completed](){
          {
            std::lock_guard lk(st->m);
            st->alive.store(false, std::memory_order_release);
          }
          if (on_completed) on_completed();
        }
      );
    });
  }
};

template <class Rep, class Period>
auto throttle_latest(std::chrono::duration<Rep, Period> win, executor& exec) {
  return throttle_latest_op<Rep, Period>{
    std::chrono::duration_cast<std::chrono::nanoseconds>(win), &exec
  };
}

} // namespace pulse
