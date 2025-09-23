#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace pulse {

struct op_debounce {
  std::chrono::milliseconds delay;
  executor* ex; // where to return on_next after debounce

  template <class T>
  auto operator()(const observable<T>& src) const {
    using namespace std::chrono;

    // shared operator state
    struct state {
      std::atomic<uint64_t> ticket{0}; // last event number
    };
    auto st = std::make_shared<state>();

    return observable<T>::create([src, st, d = delay, ex = ex]
                                 (auto on_next, auto on_err, auto on_done) {
      return src.subscribe(
        // on_next
        [st, d, ex, on_next](const T& v){
          const auto my = ++st->ticket; // my number
          // simple timer: wait for d, then check if i is the "last"
          std::thread([st, my, v, d, ex, on_next](){
            std::this_thread::sleep_for(d);
            if (st->ticket.load(std::memory_order_acquire) == my) {
              ex->post([on_next, v]{ on_next(v); });
            }
          }).detach();
        },
        // on_error
        [ex, on_err](std::exception_ptr e){
          if (on_err) ex->post([on_err, e]{ on_err(e); });
        },
        // on_completed
        [ex, on_done]{ if (on_done) ex->post(on_done); }
      );
    });
  }
};

inline auto debounce(std::chrono::milliseconds d, executor& ex){
  return op_debounce{d, &ex};
}

} // namespace pulse
