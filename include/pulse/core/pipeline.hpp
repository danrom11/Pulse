#pragma once
#include <utility>

namespace pulse {
template <class T, class Op>
auto operator|(T&& v, Op&& op) -> decltype(std::forward<Op>(op)(std::forward<T>(v))) {
  return std::forward<Op>(op)(std::forward<T>(v));
}
} // namespace pulse
