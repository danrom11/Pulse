#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>

namespace pulse {

template <class Rep, class Period>
struct throttle_op {
  std::chrono::nanoseconds win;
  executor* exec;

  template <class T>
  observable<T> operator()(const observable<T>& src) const {
    return observable<T>::create([src, win = win, exec = exec](auto on_next, auto on_error, auto on_completed) {
      struct state_t {
        std::mutex m;
        bool closed{false};
        std::atomic<bool> alive{true};
      };
      auto st = std::make_shared<state_t>();

      auto schedule_reopen = [st, win, exec]() {
        exec->post([st, win]{
          std::this_thread::sleep_for(win);
          std::lock_guard lk(st->m);
          if (!st->alive.load(std::memory_order_acquire)) return;
          st->closed = false;
        });
      };

      return src.subscribe(
        // on_next
        [st, schedule_reopen, on_next](const T& v){
          std::lock_guard lk(st->m);
          if (!st->alive.load(std::memory_order_acquire)) return;
          if (!st->closed) {
            st->closed = true;
            if (on_next) on_next(v);    // leading
            schedule_reopen();          // open the shutter at the end of the window
          }
          // otherwise we drop
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
auto throttle(std::chrono::duration<Rep, Period> win, executor& exec) {
  return throttle_op<Rep, Period>{ std::chrono::duration_cast<std::chrono::nanoseconds>(win), &exec };
}

} // namespace pulse
