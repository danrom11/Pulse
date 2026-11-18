#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// retry(k): on error, resubscribe to the source up to k times.
// k = 0 forwards the first error. The previous attempt is unsubscribed
// before the next one starts.
struct op_retry {
  std::size_t k;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, k = k](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::atomic<bool> alive{true};
        std::size_t attempts{0};
        subscription current;
        std::function<void()> start;
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;

      st->start = [w, src, k, on_next, on_err, on_done] {
        auto s = w.lock();
        if (!s || !s->alive) return;
        std::size_t my_attempt;
        {
          std::lock_guard<std::mutex> lock(s->m);
          my_attempt = s->attempts;
        }
        auto sub = src.subscribe(
            [w, on_next](const T& v) {
              auto s2 = w.lock();
              if (s2 && s2->alive && on_next) on_next(v);
            },
            [w, k, on_err](std::exception_ptr e) {
              auto s2 = w.lock();
              if (!s2 || !s2->alive) return;
              bool again = false;
              {
                std::lock_guard<std::mutex> lock(s2->m);
                if (s2->attempts < k) {
                  ++s2->attempts;
                  again = true;
                }
              }
              if (again) {
                subscription old;
                {
                  std::lock_guard<std::mutex> lock(s2->m);
                  old = std::move(s2->current);
                }
                old.reset();
                s2->start();
              } else if (s2->alive.exchange(false)) {
                if (on_err) on_err(e);
              }
            },
            [w, on_done] {
              auto s2 = w.lock();
              if (s2 && s2->alive.exchange(false) && on_done) on_done();
            });
        {
          std::lock_guard<std::mutex> lock(s->m);
          // a synchronous error may already have started the next attempt
          if (s->alive && s->attempts == my_attempt) s->current = std::move(sub);
        }
        sub.reset();
      };

      st->start();

      return subscription([st] {
        st->alive = false;
        subscription cur;
        {
          std::lock_guard<std::mutex> lock(st->m);
          cur = std::move(st->current);
        }
        cur.reset();
        st->start = nullptr;
      });
    });
  }
};

inline auto retry(std::size_t k) { return op_retry{k}; }

} // namespace pulse
