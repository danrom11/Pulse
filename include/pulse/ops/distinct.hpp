#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_set>

#include <pulse/core/observable.hpp>

namespace pulse {

// distinct_until_changed(): drops a value equal to the previous one.
// distinct_until_changed(key_fn): compares key_fn(value) instead.
// State is per subscription.
template <class KeyFn = void>
struct op_distinct_until_changed {
  KeyFn key;

  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, key = key](auto on_next, auto on_err, auto on_done) {
      using K = std::decay_t<std::invoke_result_t<const KeyFn&, const T&>>;
      struct state {
        std::mutex m;
        std::optional<K> prev;
      };
      auto st = std::make_shared<state>();
      return src.subscribe(
          [st, key, on_next](const T& v) {
            K k = key(v);
            {
              std::lock_guard<std::mutex> lock(st->m);
              if (st->prev && *st->prev == k) return;
              st->prev = std::move(k);
            }
            on_next(v);
          },
          on_err, on_done);
    });
  }
};

template <>
struct op_distinct_until_changed<void> {
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::optional<T> prev;
      };
      auto st = std::make_shared<state>();
      return src.subscribe(
          [st, on_next](const T& v) {
            {
              std::lock_guard<std::mutex> lock(st->m);
              if (st->prev && *st->prev == v) return;
              st->prev = v;
            }
            on_next(v);
          },
          on_err, on_done);
    });
  }
};

inline auto distinct_until_changed() { return op_distinct_until_changed<void>{}; }

template <class KeyFn>
inline auto distinct_until_changed(KeyFn key) {
  return op_distinct_until_changed<KeyFn>{std::move(key)};
}

// distinct(): drops every value that was already seen (requires std::hash).
struct op_distinct {
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::unordered_set<T> seen;
      };
      auto st = std::make_shared<state>();
      return src.subscribe(
          [st, on_next](const T& v) {
            {
              std::lock_guard<std::mutex> lock(st->m);
              if (!st->seen.insert(v).second) return;
            }
            on_next(v);
          },
          on_err, on_done);
    });
  }
};

inline auto distinct() { return op_distinct{}; }

} // namespace pulse
