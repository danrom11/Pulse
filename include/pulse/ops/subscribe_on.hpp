#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/scheduler.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace pulse {

// ================================
// subscribe_on(executor&)
// IMPORTANT: The executor must live longer than the subscription!
// ===============================
struct op_subscribe_on {
  executor* ex;

  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, ex = ex](auto on_next, auto on_error, auto on_completed) {
      struct state {
        std::atomic<bool> alive{true};
        subscription up;
      };
      auto st = std::make_shared<state>();
      auto wst = std::weak_ptr<state>(st);

      // transfer the subscription itself to the specified executor
      ex->post([wst, src, on_next, on_error, on_completed]{
        auto s = wst.lock();
        if (!s || !s->alive) return; // unsubscribed before they had time to subscribe
        s->up = src.subscribe(
          // we simply forward downstream callbacks
          [wst, on_next](const T& v){
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              if (on_next) on_next(v);
            }
          },
          [wst, on_error](std::exception_ptr e){
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              s2->alive = false;
              s2->up.reset();
              if (on_error) on_error(e);
            }
          },
          [wst, on_completed]{
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              s2->alive = false;
              s2->up.reset();
              if (on_completed) on_completed();
            }
          }
        );
      });

      // unsubscribe: extinguish alive and upstream
      return subscription([st]{
        if (!st) return;
        st->alive = false;
        st->up.reset();
      });
    });
  }
};

inline auto subscribe_on(executor& ex){ return op_subscribe_on{ &ex }; }

// ================================
// subscribe_on(shared_ptr<executor>)
// Keep the executor alive until completion
// ================================
struct op_subscribe_on_sp {
  std::shared_ptr<executor> ex;

  template <class T>
  auto operator()(const observable<T>& src) const {
    auto exec = ex;
    return observable<T>::create([src, exec](auto on_next, auto on_error, auto on_completed) {
      struct state {
        std::atomic<bool> alive{true};
        subscription up;
      };
      auto st = std::make_shared<state>();
      auto wst = std::weak_ptr<state>(st);

      exec->post([wst, src, on_next, on_error, on_completed]{
        auto s = wst.lock();
        if (!s || !s->alive) return;
        s->up = src.subscribe(
          [wst, on_next](const T& v){
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              if (on_next) on_next(v);
            }
          },
          [wst, on_error](std::exception_ptr e){
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              s2->alive = false;
              s2->up.reset();
              if (on_error) on_error(e);
            }
          },
          [wst, on_completed]{
            if (auto s2 = wst.lock(); s2 && s2->alive) {
              s2->alive = false;
              s2->up.reset();
              if (on_completed) on_completed();
            }
          }
        );
      });

      return subscription([st]{
        if (!st) return;
        st->alive = false;
        st->up.reset();
      });
    });
  }
};

inline auto subscribe_on(std::shared_ptr<executor> ex){
  return op_subscribe_on_sp{ std::move(ex) };
}

} // namespace pulse
