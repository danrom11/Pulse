#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// delay(d, ex): shifts every event (values and completion) by `d`, delivered
// on `ex`. Errors are forwarded immediately. Pending events are dropped on
// unsubscribe.
struct op_delay {
  std::chrono::nanoseconds d;
  executor* ex;

  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, d = d, ex = ex](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::atomic<bool> alive{true};
        std::uint64_t next{1};
        std::map<std::uint64_t, subscription> timers;
        subscription upstream;
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;

      auto schedule = [w, d, ex](std::function<void()> fn) {
        auto s = w.lock();
        if (!s || !s->alive) return;
        std::uint64_t id;
        {
          std::lock_guard<std::mutex> lock(s->m);
          id = s->next++;
        }
        auto t = timer_service::instance().schedule_after(d, [w, id, ex, fn] {
          auto s2 = w.lock();
          if (!s2 || !s2->alive) return;
          {
            std::lock_guard<std::mutex> lock(s2->m);
            auto it = s2->timers.find(id);
            if (it != s2->timers.end()) {
              it->second.release();
              s2->timers.erase(it);
            }
          }
          ex->post([w, fn] {
            auto s3 = w.lock();
            if (s3 && s3->alive) fn();
          });
        });
        std::lock_guard<std::mutex> lock(s->m);
        s->timers.emplace(id, std::move(t));
      };

      auto up = src.subscribe(
          [schedule, on_next](const T& v) {
            schedule([on_next, v] {
              if (on_next) on_next(v);
            });
          },
          [w, ex, on_err](std::exception_ptr e) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            ex->post([w, on_err, e] {
              auto s2 = w.lock();
              if (s2 && s2->alive.exchange(false) && on_err) on_err(e);
            });
          },
          [schedule, on_done] {
            schedule([on_done] {
              if (on_done) on_done();
            });
          });
      {
        std::lock_guard<std::mutex> lock(st->m);
        st->upstream = std::move(up);
      }
      return subscription([st] {
        st->alive = false;
        std::map<std::uint64_t, subscription> timers;
        subscription u;
        {
          std::lock_guard<std::mutex> lock(st->m);
          timers.swap(st->timers);
          u = std::move(st->upstream);
        }
        timers.clear();
        u.reset();
      });
    });
  }
};

template <class Rep, class Period>
inline auto delay(std::chrono::duration<Rep, Period> d, executor& ex) {
  return op_delay{std::chrono::duration_cast<std::chrono::nanoseconds>(d), &ex};
}

} // namespace pulse
