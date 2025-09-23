#pragma once
#include <pulse/core/observable.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>

namespace pulse {

// If the source does not return a completion value/event in time, generate on_error(timeout)
template <class Rep, class Period>
struct op_timeout {
  std::chrono::duration<Rep,Period> d;

  template <class T>
  auto operator()(const observable<T>& src) const {
    using namespace std::chrono;

    return observable<T>::create([src, d = d](auto on_next, auto on_err, auto on_done){
      // is the "timeout timer" alive (reset on first event)
      auto alive = std::make_shared<std::atomic<bool>>(true);

      // watchdog thread - a simple but robust implementation for examples
      std::thread([alive, d, on_err](){
        std::this_thread::sleep_for(d);
        if (alive->exchange(false, std::memory_order_acq_rel)) {
          if (on_err) on_err(std::make_exception_ptr(std::runtime_error("timeout")));
        }
      }).detach();

      return src.subscribe(
        // any event "extinguishes" the timer
        [alive, on_next](const T& v){
          if (alive->exchange(false, std::memory_order_acq_rel)) {
            on_next(v);
          } else {
            // the timeout has already occurred - you can ignore/forward it as you wish
          }
        },
        [alive, on_err](std::exception_ptr e){
          if (alive->exchange(false, std::memory_order_acq_rel)) {
            if (on_err) on_err(e);
          }
        },
        [alive, on_done]{
          if (alive->exchange(false, std::memory_order_acq_rel)) {
            if (on_done) on_done();
          }
        }
      );
    });
  }
};

template <class Rep, class Period>
inline auto timeout(std::chrono::duration<Rep,Period> d) { return op_timeout<Rep,Period>{ d }; }

} // namespace pulse
