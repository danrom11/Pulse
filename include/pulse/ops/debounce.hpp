#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// debounce(d, ex): emits a value only after `d` passed without a newer value.
// Values are delivered on `ex`. On completion the pending value is flushed.
struct op_debounce {
  std::chrono::nanoseconds delay;
  executor* ex;

  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, d = delay, ex = ex](auto on_next, auto on_err,
                                                           auto on_done) {
      struct state {
        std::mutex m;
        std::uint64_t ticket{0};
        std::optional<T> pending;
        subscription timer;
        std::atomic<bool> alive{true};
        subscription upstream;
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;

      auto up = src.subscribe(
          [w, d, ex, on_next](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            std::uint64_t my;
            subscription old;
            {
              std::lock_guard<std::mutex> lock(s->m);
              my = ++s->ticket;
              s->pending = v;
              old = std::move(s->timer);
            }
            old.reset();
            auto t = timer_service::instance().schedule_after(d, [w, my, ex, on_next] {
              auto s2 = w.lock();
              if (!s2 || !s2->alive) return;
              std::optional<T> out;
              {
                std::lock_guard<std::mutex> lock(s2->m);
                if (s2->ticket != my || !s2->pending) return;
                out = std::move(s2->pending);
                s2->pending.reset();
              }
              ex->post([w, on_next, val = std::move(*out)] {
                auto s3 = w.lock();
                if (s3 && s3->alive && on_next) on_next(val);
              });
            });
            std::lock_guard<std::mutex> lock(s->m);
            if (s->ticket == my) s->timer = std::move(t);
          },
          [w, ex, on_err](std::exception_ptr e) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            ex->post([w, on_err, e] {
              auto s2 = w.lock();
              if (s2 && s2->alive.exchange(false) && on_err) on_err(e);
            });
          },
          [w, ex, on_next, on_done] {
            auto s = w.lock();
            if (!s || !s->alive) return;
            std::optional<T> last;
            {
              std::lock_guard<std::mutex> lock(s->m);
              last = std::move(s->pending);
              s->pending.reset();
              ++s->ticket;
            }
            ex->post([w, on_next, on_done, last] {
              auto s2 = w.lock();
              if (!s2 || !s2->alive.exchange(false)) return;
              if (last && on_next) on_next(*last);
              if (on_done) on_done();
            });
          });
      st->upstream = std::move(up);

      return subscription([st] {
        st->alive = false;
        subscription t;
        {
          std::lock_guard<std::mutex> lock(st->m);
          t = std::move(st->timer);
        }
        t.reset();
        st->upstream.reset();
      });
    });
  }
};

template <class Rep, class Period>
inline auto debounce(std::chrono::duration<Rep, Period> d, executor& ex) {
  return op_debounce{std::chrono::duration_cast<std::chrono::nanoseconds>(d), &ex};
}

} // namespace pulse
