#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// window(count): splits the source into consecutive inner observables of
// `count` values each. Inner observables are hot: subscribe to them inside
// the outer on_next to receive their values.
struct op_window_count {
  std::size_t count;

  template <class T>
  auto operator()(const observable<T>& src) const {
    if (count == 0) throw std::invalid_argument("pulse::window(count): count must be > 0");
    using InnerObs = observable<T>;

    struct window_state {
      std::mutex m;
      std::function<void(const T&)> on_next;
      std::function<void(std::exception_ptr)> on_error;
      std::function<void()> on_completed;
      bool open{true};
    };

    struct outer_state {
      std::mutex m;
      std::atomic<bool> alive{true};
      std::shared_ptr<window_state> current;
      std::size_t filled{0};
      subscription upstream;
    };

    const std::size_t n = count;

    return observable<InnerObs>::create([src, n](auto on_next_outer, auto on_err_outer,
                                                 auto on_done_outer) {
      auto st = std::make_shared<outer_state>();
      std::weak_ptr<outer_state> w = st;

      auto make_inner = [](std::shared_ptr<window_state> ws) {
        return InnerObs::create([ws](auto on_next, auto on_err, auto on_done) {
          std::lock_guard<std::mutex> lock(ws->m);
          ws->on_next = std::move(on_next);
          ws->on_error = std::move(on_err);
          ws->on_completed = std::move(on_done);
          std::weak_ptr<window_state> wws = ws;
          // Discarding the returned subscription keeps the window subscribed
          // (it ends with the window); call reset() to leave early.
          return subscription([wws] {
            if (auto x = wws.lock()) {
              std::lock_guard<std::mutex> lock(x->m);
              x->on_next = nullptr;
              x->on_error = nullptr;
              x->on_completed = nullptr;
            }
          }, /*cancel_on_dtor=*/false);
        });
      };

      auto close = [](const std::shared_ptr<window_state>& ws, std::exception_ptr e) {
        if (!ws) return;
        std::function<void()> done;
        std::function<void(std::exception_ptr)> err;
        {
          std::lock_guard<std::mutex> lock(ws->m);
          if (!ws->open) return;
          ws->open = false;
          done = ws->on_completed;
          err = ws->on_error;
        }
        if (e) {
          if (err) err(e);
        } else if (done) {
          done();
        }
      };

      auto up = src.subscribe(
          [w, n, make_inner, close, on_next_outer](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            std::shared_ptr<window_state> ws;
            bool opened = false;
            {
              std::lock_guard<std::mutex> lock(s->m);
              if (!s->current) {
                s->current = std::make_shared<window_state>();
                s->filled = 0;
                opened = true;
              }
              ws = s->current;
            }
            if (opened && on_next_outer) on_next_outer(make_inner(ws));

            std::function<void(const T&)> fn;
            {
              std::lock_guard<std::mutex> lock(ws->m);
              fn = ws->on_next;
            }
            if (fn) fn(v);

            bool full = false;
            {
              std::lock_guard<std::mutex> lock(s->m);
              if (++s->filled >= n) {
                full = true;
                s->current.reset();
                s->filled = 0;
              }
            }
            if (full) close(ws, nullptr);
          },
          [w, close, on_err_outer](std::exception_ptr e) {
            auto s = w.lock();
            if (!s || !s->alive.exchange(false)) return;
            std::shared_ptr<window_state> ws;
            {
              std::lock_guard<std::mutex> lock(s->m);
              ws = std::move(s->current);
            }
            close(ws, e);
            if (on_err_outer) on_err_outer(e);
          },
          [w, close, on_done_outer] {
            auto s = w.lock();
            if (!s || !s->alive.exchange(false)) return;
            std::shared_ptr<window_state> ws;
            {
              std::lock_guard<std::mutex> lock(s->m);
              ws = std::move(s->current);
            }
            close(ws, nullptr);
            if (on_done_outer) on_done_outer();
          });

      {
        std::lock_guard<std::mutex> lock(st->m);
        st->upstream = std::move(up);
      }

      return subscription([st] {
        st->alive = false;
        subscription u;
        std::shared_ptr<window_state> ws;
        {
          std::lock_guard<std::mutex> lock(st->m);
          u = std::move(st->upstream);
          ws = std::move(st->current);
        }
        if (ws) {
          std::lock_guard<std::mutex> lock(ws->m);
          ws->on_next = nullptr;
          ws->on_error = nullptr;
          ws->on_completed = nullptr;
        }
        u.reset();
      });
    });
  }
};

inline auto window(std::size_t count) { return op_window_count{count}; }

} // namespace pulse
