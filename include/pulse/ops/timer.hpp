#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/scheduler.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <memory>

namespace pulse {

// single-shot timer: via due, issue one event (0) and exit
inline observable<int> timer(std::chrono::milliseconds due, executor& ex) {
  return observable<int>::create([due, &ex](auto on_next, auto on_err, auto on_done){
    auto alive = std::make_shared<std::atomic<bool>>(true);
    std::thread([alive, due, &ex, on_next, on_done]{
      std::this_thread::sleep_for(due);
      if (!alive->load(std::memory_order_acquire)) return;
      ex.post([alive, on_next, on_done]{
        if (!alive->load(std::memory_order_acquire)) return;
        if (on_next) on_next(0);
        if (on_done) on_done();
      });
    }).detach();
    return subscription([alive]{ alive->store(false, std::memory_order_release); });
  });
}

// interval: periodically issues ticks (0,1,2,...) until unsubscribed
inline observable<std::size_t> interval(std::chrono::milliseconds period, executor& ex,
                                        std::chrono::milliseconds initial_delay = std::chrono::milliseconds{0}) {
  return observable<std::size_t>::create([period, initial_delay, &ex](auto on_next, auto, auto){
    auto alive = std::make_shared<std::atomic<bool>>(true);
    std::thread([alive, period, initial_delay, &ex, on_next]{
      if (initial_delay.count() > 0)
        std::this_thread::sleep_for(initial_delay);
      std::size_t tick = 0;
      while (alive->load(std::memory_order_acquire)) {
        ex.post([alive, on_next, tick]{
          if (alive->load(std::memory_order_acquire) && on_next) on_next(tick);
        });
        ++tick;
        std::this_thread::sleep_for(period);
      }
    }).detach();
    return subscription([alive]{ alive->store(false, std::memory_order_release); });
  });
}

} // namespace pulse
