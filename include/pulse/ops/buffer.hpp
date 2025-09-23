#pragma once

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <cstddef>
#include <vector>
#include <memory>
#include <atomic>
#include <utility>
#include <stdexcept>

namespace pulse {

// buffer(count)
// - every count elements is returned by std::vector<T>
// - on_completed adds the "tail"
// - on_error does NOT add the tail (an error occurs immediately)
// - the state is simply cleared upon unsubscribing
struct op_buffer_count {
  std::size_t count;

  template <class T>
  auto operator()(const observable<T>& src) const {
    if (count == 0)
      throw std::invalid_argument("pulse::buffer(count): count must be > 0");

    using Vec = std::vector<T>;

    return observable<Vec>::create([src, n = count](auto on_next, auto on_err, auto on_done) {
      auto alive  = std::make_shared<std::atomic<bool>>(true);
      auto buf    = std::make_shared<Vec>();
      buf->reserve(n);

      auto flush_full = [buf, n, on_next]() {
        if (buf->size() >= n) {
          if (on_next) on_next(*buf);
          buf->clear();
          buf->reserve(n);
        }
      };

      auto flush_tail_and_done = [buf, on_next, on_done]() {
        if (!buf->empty()) {
          if (on_next) on_next(*buf);
          buf->clear();
        }
        if (on_done) on_done();
      };

      // subscription to upstream
      auto upstream = src.subscribe(
        // on_next
        [alive, buf, n, on_next, flush_full](const T& v){
          if (!*alive) return;
          buf->push_back(v);
          if (buf->size() >= n) flush_full();
        },
        // on_error — tail is not emitted
        [alive, on_err](std::exception_ptr e){
          if (!*alive) return;
          if (on_err) on_err(e);
        },
        // on_completed - tail + done
        [alive, buf, on_next, on_done, flush_tail_and_done]{
          if (!*alive) return;
          flush_tail_and_done();
        }
      );

      // IMPORTANT: Hide the move-only subscription in a shared_ptr,
      // so that the unsubscribe lambda is copyable (a std::function requirement)
      auto up = std::make_shared<subscription>(std::move(upstream));

      return subscription([alive, up, buf]() mutable {
        *alive = false;
        if (up) up->reset();
      });
    });
  }
};

// pipeable helper
inline auto buffer(std::size_t count) {
  return op_buffer_count{ count };
}

} // namespace pulse
