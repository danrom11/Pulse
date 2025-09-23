#pragma once
#include <pulse/core/observable.hpp>
#include <memory>
#include <optional>

namespace pulse {

// Skips an element only if it is different from the previous one (operator==)
struct op_distinct_until_changed {
  template <class T>
  auto operator()(const observable<T>& src) const {
    auto prev = std::make_shared<std::optional<T>>();
    return observable<T>::create([src, prev](auto on_next, auto on_err, auto on_done){
      return src.subscribe(
        [prev, on_next](const T& v){
          if (!*prev || **prev != v) { *prev = v; on_next(v); }
        },
        on_err,
        on_done
      );
    });
  }
};

inline auto distinct_until_changed() { return op_distinct_until_changed{}; }

} // namespace pulse
