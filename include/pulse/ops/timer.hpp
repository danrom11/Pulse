#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// Single-shot timer: after `due`, emits 0 on `ex` and completes.
// The executor must outlive the subscription.
template <class Rep, class Period>
inline observable<int> timer(std::chrono::duration<Rep, Period> due, executor& ex) {
  return observable<int>::create([due, &ex](auto on_next, auto, auto on_done) {
    auto alive = std::make_shared<std::atomic<bool>>(true);
    auto t = timer_service::instance().schedule_after(due, [alive, &ex, on_next, on_done] {
      if (!alive->load(std::memory_order_acquire)) return;
      ex.post([alive, on_next, on_done] {
        if (!alive->exchange(false, std::memory_order_acq_rel)) return;
        if (on_next) on_next(0);
        if (on_done) on_done();
      });
    });
    auto tp = std::make_shared<subscription>(std::move(t));
    return subscription([alive, tp] {
      alive->store(false, std::memory_order_release);
      tp->reset();
    });
  });
}

// Periodic ticks 0, 1, 2, ... on `ex` until unsubscribed.
template <class Rep, class Period>
inline observable<std::size_t> interval(std::chrono::duration<Rep, Period> period, executor& ex,
                                        std::chrono::milliseconds initial_delay =
                                            std::chrono::milliseconds{0}) {
  return observable<std::size_t>::create([period, initial_delay, &ex](auto on_next, auto, auto) {
    auto alive = std::make_shared<std::atomic<bool>>(true);
    auto tick = std::make_shared<std::atomic<std::size_t>>(0);
    auto t = timer_service::instance().schedule_periodic(
        initial_delay, period, [alive, tick, &ex, on_next] {
          if (!alive->load(std::memory_order_acquire)) return;
          const std::size_t n = tick->fetch_add(1, std::memory_order_relaxed);
          ex.post([alive, on_next, n] {
            if (alive->load(std::memory_order_acquire) && on_next) on_next(n);
          });
        });
    auto tp = std::make_shared<subscription>(std::move(t));
    return subscription([alive, tp] {
      alive->store(false, std::memory_order_release);
      tp->reset();
    });
  });
}

} // namespace pulse
