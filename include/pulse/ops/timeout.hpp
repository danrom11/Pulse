#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

struct timeout_error : std::runtime_error {
  timeout_error() : std::runtime_error("pulse::timeout") {}
};

// timeout(d): errors with timeout_error if no event arrives within `d`
// after subscription, or within `d` after the previous value (idle timeout).
// All values pass through until then. The timer is cancelled on unsubscribe.
template <class Rep, class Period>
struct op_timeout {
  std::chrono::duration<Rep, Period> d;

  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, d = d](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::atomic<bool> alive{true};
        std::uint64_t gen{0};
        subscription timer;
        subscription upstream;
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;

      auto arm = [w, d, on_err] {
        auto s = w.lock();
        if (!s || !s->alive) return;
        std::uint64_t my;
        subscription old;
        {
          std::lock_guard<std::mutex> lock(s->m);
          my = ++s->gen;
          old = std::move(s->timer);
        }
        old.reset();
        auto t = timer_service::instance().schedule_after(d, [w, my, on_err] {
          auto s2 = w.lock();
          if (!s2) return;
          {
            std::lock_guard<std::mutex> lock(s2->m);
            if (s2->gen != my) return;
          }
          if (!s2->alive.exchange(false)) return;
          subscription up;
          {
            std::lock_guard<std::mutex> lock(s2->m);
            up = std::move(s2->upstream);
          }
          up.reset();
          if (on_err) on_err(std::make_exception_ptr(timeout_error{}));
        });
        std::lock_guard<std::mutex> lock(s->m);
        if (s->gen == my) s->timer = std::move(t);
      };

      arm();
      auto up = src.subscribe(
          [w, on_next, arm](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            arm();
            if (on_next) on_next(v);
          },
          [w, on_err](std::exception_ptr e) {
            auto s = w.lock();
            if (!s || !s->alive.exchange(false)) return;
            if (on_err) on_err(e);
          },
          [w, on_done] {
            auto s = w.lock();
            if (!s || !s->alive.exchange(false)) return;
            if (on_done) on_done();
          });
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->alive) st->upstream = std::move(up);
      }
      up.reset(); // no-op unless the stream already terminated

      return subscription([st] {
        st->alive = false;
        subscription t, up;
        {
          std::lock_guard<std::mutex> lock(st->m);
          ++st->gen;
          t = std::move(st->timer);
          up = std::move(st->upstream);
        }
        t.reset();
        up.reset();
      });
    });
  }
};

template <class Rep, class Period>
inline auto timeout(std::chrono::duration<Rep, Period> d) {
  return op_timeout<Rep, Period>{d};
}

} // namespace pulse
