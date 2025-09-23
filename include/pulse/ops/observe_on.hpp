#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <utility>

namespace pulse {

// ----------------------------
// observe_on(executor&)
// IMPORTANT: ex must outlive the subscription!
// ----------------------------
struct op_observe_on {
  executor* ex;

  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, ex = ex](auto on_next, auto on_err, auto on_done) {
      auto alive = std::make_shared<std::atomic<bool>>(true);

      auto up = src.subscribe(
        [ex, on_next, alive](const T& v){
          if (!*alive) return;
          ex->post([on_next, v, alive]{
            if (!*alive) return;
            if (on_next) on_next(v);
          });
        },
        [ex, on_err, alive](std::exception_ptr e){
          if (!*alive) return;
          ex->post([on_err, e, alive]{
            if (!*alive) return;
            if (on_err) on_err(e);
          });
        },
        [ex, on_done, alive]{
          if (!*alive) return;
          ex->post([on_done, alive]{
            if (!*alive) return;
            if (on_done) on_done();
          });
        }
      );

      auto up_ptr = std::make_shared<subscription>(std::move(up));

      return subscription([alive, up_ptr]() mutable {
        *alive = false;
        *up_ptr = subscription{};
      });
    });
  }
};

inline auto observe_on(executor& ex){ return op_observe_on{ &ex }; }

// ----------------------------
// observe_on(shared_ptr<executor>)
// Safely keeps the executor alive (recommended, see Qt Adapter)
// ----------------------------
struct op_observe_on_sp {
  std::shared_ptr<executor> ex;

  template <class T>
  auto operator()(const observable<T>& src) const {
    auto exec = ex;
    return observable<T>::create([src, exec](auto on_next, auto on_err, auto on_done) {
      auto alive = std::make_shared<std::atomic<bool>>(true);

      auto up = src.subscribe(
        [exec, on_next, alive](const T& v){
          if (!*alive) return;
          exec->post([on_next, v, alive]{
            if (!*alive) return;
            if (on_next) on_next(v);
          });
        },
        [exec, on_err, alive](std::exception_ptr e){
          if (!*alive) return;
          exec->post([on_err, e, alive]{
            if (!*alive) return;
            if (on_err) on_err(e);
          });
        },
        [exec, on_done, alive]{
          if (!*alive) return;
          exec->post([on_done, alive]{
            if (!*alive) return;
            if (on_done) on_done();
          });
        }
      );

      auto up_ptr = std::make_shared<subscription>(std::move(up));

      return subscription([alive, up_ptr]() mutable {
        *alive = false;
        *up_ptr = subscription{};
      });
    });
  }
};

inline auto observe_on(std::shared_ptr<executor> ex){
  return op_observe_on_sp{ std::move(ex) };
}

} // namespace pulse
