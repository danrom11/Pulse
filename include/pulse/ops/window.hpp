#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <utility>
#include <cstddef>

namespace pulse {

struct op_window_count {
  std::size_t count;

  template <class T>
  auto operator()(const observable<T>& src) const {
    using InnerObs = observable<T>;

    struct WindowState {
      std::function<void(const T&)>           on_next;
      std::function<void()>                   on_completed;
      std::function<void(std::exception_ptr)> on_error;
      std::atomic<bool> subscribed{false};
      std::atomic<bool> open{true};
    };

    auto make_inner = [](std::shared_ptr<WindowState> st) -> InnerObs {
      return InnerObs::create([st](auto on_next, auto on_err, auto on_done){
        st->on_next      = std::move(on_next);
        st->on_error     = std::move(on_err);
        st->on_completed = std::move(on_done);
        st->subscribed   = true;

        return subscription{};
      });
    };

    const std::size_t n = count;

    return observable<InnerObs>::create([src, n, make_inner](auto on_next_outer, auto on_err_outer, auto on_done_outer){
      auto alive = std::make_shared<std::atomic<bool>>(true);

      // The current active window (as a shared_ptr inside a shared_ptr - convenient to reset in place).
      auto curp = std::make_shared<std::shared_ptr<WindowState>>();
      std::size_t filled = 0;

      auto open_window = [&]{
        *curp = std::make_shared<WindowState>();
        filled = 0;
        if (on_next_outer) {
          on_next_outer(make_inner(*curp));
        }
      };

      auto close_window = [&]{
        if (!*curp) return;
        (*curp)->open = false;
        if ((*curp)->on_completed) (*curp)->on_completed();
        *curp = nullptr;
        filled = 0;
      };

      auto up = src.subscribe(
        // The order is important: open if needed -> next(v) -> ++filled -> if n is reached — complete the window.
        [=, &filled](const T& v){
          if (!*alive) return;

          if (!*curp || !(*curp)->open) {
            open_window();
          }

          if (*curp && (*curp)->open && (*curp)->on_next) {
            (*curp)->on_next(v);
          }

          ++filled;
          if (filled >= n) {
            close_window();
          }
        },
        [=](std::exception_ptr e){
          if (!*alive) return;

          if (*curp && (*curp)->open) {
            (*curp)->open = false;
            if ((*curp)->on_error) (*curp)->on_error(e);
            *curp = nullptr;
          }
          if (on_err_outer) on_err_outer(e);
        },
        [=]{
          if (!*alive) return;

          if (*curp && (*curp)->open) {
            (*curp)->open = false;
            if ((*curp)->on_completed) (*curp)->on_completed();
            *curp = nullptr;
          }
          if (on_done_outer) on_done_outer();
        }
      );

      auto uph = std::make_shared<subscription>(std::move(up));

      return subscription([alive, curp, uph]{
        *alive = false;
        if (*curp) {
          (*curp)->on_next = nullptr;
          (*curp)->on_error = nullptr;
          (*curp)->on_completed = nullptr;
          *curp = nullptr;
        }
        if (uph) uph->reset();
      });
    });
  }
};

inline auto window(std::size_t count){
  return op_window_count{count};
}

} // namespace pulse
