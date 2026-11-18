#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// skip(n): drops the first n values.
struct op_skip {
  std::size_t n;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, n = n](auto on_next, auto on_err, auto on_done) {
      auto left = std::make_shared<std::atomic<std::size_t>>(n);
      return src.subscribe(
          [left, on_next](const T& v) {
            std::size_t rem = left->load(std::memory_order_acquire);
            while (rem > 0) {
              if (left->compare_exchange_weak(rem, rem - 1)) return;
            }
            if (on_next) on_next(v);
          },
          on_err, on_done);
    });
  }
};
inline auto skip(std::size_t n) { return op_skip{n}; }

// skip_while(pred): drops values while pred(v) is true, then passes everything.
template <class P>
struct op_skip_while {
  P pred;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, pred = pred](auto on_next, auto on_err, auto on_done) {
      auto passing = std::make_shared<std::atomic<bool>>(false);
      return src.subscribe(
          [passing, pred, on_next](const T& v) {
            if (!passing->load()) {
              if (pred(v)) return;
              passing->store(true);
            }
            if (on_next) on_next(v);
          },
          on_err, on_done);
    });
  }
};
template <class P>
inline auto skip_while(P p) {
  return op_skip_while<P>{std::move(p)};
}

// take_while(pred): passes values while pred(v) is true, then completes.
template <class P>
struct op_take_while {
  P pred;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, pred = pred](auto on_next, auto on_err, auto on_done) {
      auto finished = std::make_shared<std::atomic<bool>>(false);
      auto comp = std::make_shared<composite_subscription>();
      comp->add(src.subscribe(
          [finished, pred, on_next, on_done, comp](const T& v) {
            if (finished->load()) return;
            if (pred(v)) {
              if (on_next) on_next(v);
            } else if (!finished->exchange(true)) {
              if (on_done) on_done();
              comp->reset();
            }
          },
          [finished, on_err, comp](std::exception_ptr e) {
            if (finished->exchange(true)) return;
            if (on_err) on_err(e);
            comp->reset();
          },
          [finished, on_done, comp] {
            if (finished->exchange(true)) return;
            if (on_done) on_done();
            comp->reset();
          }));
      return subscription([comp] { comp->reset(); });
    });
  }
};
template <class P>
inline auto take_while(P p) {
  return op_take_while<P>{std::move(p)};
}

// take_until(notifier): passes values until `notifier` emits, then completes.
template <class N>
struct op_take_until {
  observable<N> notifier;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, notifier = notifier](auto on_next, auto on_err,
                                                            auto on_done) {
      auto finished = std::make_shared<std::atomic<bool>>(false);
      auto comp = std::make_shared<composite_subscription>();
      comp->add(notifier.subscribe([finished, on_done, comp](const N&) {
        if (finished->exchange(true)) return;
        if (on_done) on_done();
        comp->reset();
      }));
      if (finished->load()) return subscription{};
      comp->add(src.subscribe(
          [finished, on_next](const T& v) {
            if (!finished->load() && on_next) on_next(v);
          },
          [finished, on_err, comp](std::exception_ptr e) {
            if (finished->exchange(true)) return;
            if (on_err) on_err(e);
            comp->reset();
          },
          [finished, on_done, comp] {
            if (finished->exchange(true)) return;
            if (on_done) on_done();
            comp->reset();
          }));
      return subscription([comp] { comp->reset(); });
    });
  }
};
template <class N>
inline auto take_until(observable<N> notifier) {
  return op_take_until<N>{std::move(notifier)};
}

// first(): the first value, then completes. Errors if the source is empty.
struct op_first {
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src](auto on_next, auto on_err, auto on_done) {
      auto finished = std::make_shared<std::atomic<bool>>(false);
      auto comp = std::make_shared<composite_subscription>();
      comp->add(src.subscribe(
          [finished, on_next, on_done, comp](const T& v) {
            if (finished->exchange(true)) return;
            if (on_next) on_next(v);
            if (on_done) on_done();
            comp->reset();
          },
          [finished, on_err](std::exception_ptr e) {
            if (!finished->exchange(true) && on_err) on_err(e);
          },
          [finished, on_err] {
            if (!finished->exchange(true) && on_err)
              on_err(std::make_exception_ptr(std::out_of_range("pulse::first: empty sequence")));
          }));
      return subscription([comp] { comp->reset(); });
    });
  }
};
inline auto first() { return op_first{}; }

// last(): the last value when the source completes. Errors if empty.
struct op_last {
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::optional<T> v;
      };
      auto st = std::make_shared<state>();
      return src.subscribe(
          [st](const T& v) {
            std::lock_guard<std::mutex> lock(st->m);
            st->v = v;
          },
          on_err,
          [st, on_next, on_done, on_err] {
            std::optional<T> v;
            {
              std::lock_guard<std::mutex> lock(st->m);
              v = st->v;
            }
            if (!v) {
              if (on_err)
                on_err(std::make_exception_ptr(std::out_of_range("pulse::last: empty sequence")));
              return;
            }
            if (on_next) on_next(*v);
            if (on_done) on_done();
          });
    });
  }
};
inline auto last() { return op_last{}; }

} // namespace pulse
