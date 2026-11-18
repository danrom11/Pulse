#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include <pulse/core/observable.hpp>

namespace pulse {

// tap(fn): side effect for every value, the value passes through unchanged.
template <class F>
struct op_tap {
  F f;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, f = f](auto on_next, auto on_err, auto on_done) {
      return src.subscribe(
          [f, on_next](const T& v) {
            f(v);
            if (on_next) on_next(v);
          },
          on_err, on_done);
    });
  }
};
template <class F>
inline auto tap(F f) {
  return op_tap<F>{std::move(f)};
}

// finally_do(fn): runs fn once when the stream completes, errors or is unsubscribed.
template <class F>
struct op_finally {
  F f;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, f = f](auto on_next, auto on_err, auto on_done) {
      auto once = std::make_shared<std::once_flag>();
      auto run = [once, f] { std::call_once(*once, f); };
      auto up = src.subscribe(
          on_next,
          [on_err, run](std::exception_ptr e) {
            if (on_err) on_err(e);
            run();
          },
          [on_done, run] {
            if (on_done) on_done();
            run();
          });
      auto sp = std::make_shared<subscription>(std::move(up));
      return subscription([sp, run] {
        sp->reset();
        run();
      });
    });
  }
};
template <class F>
inline auto finally_do(F f) {
  return op_finally<F>{std::move(f)};
}

// scan(seed, acc): emits every intermediate accumulation.
template <class S, class F>
struct op_scan {
  S seed;
  F f;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<S>::create([src, seed = seed, f = f](auto on_next, auto on_err,
                                                           auto on_done) {
      struct state {
        std::mutex m;
        S acc;
      };
      auto st = std::make_shared<state>();
      st->acc = seed;
      return src.subscribe(
          [st, f, on_next](const T& v) {
            S out;
            {
              std::lock_guard<std::mutex> lock(st->m);
              st->acc = f(st->acc, v);
              out = st->acc;
            }
            if (on_next) on_next(out);
          },
          on_err, on_done);
    });
  }
};
template <class S, class F>
inline auto scan(S seed, F f) {
  return op_scan<S, F>{std::move(seed), std::move(f)};
}

// reduce(seed, acc): emits only the final accumulation when the source completes.
template <class S, class F>
struct op_reduce {
  S seed;
  F f;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<S>::create([src, seed = seed, f = f](auto on_next, auto on_err,
                                                           auto on_done) {
      struct state {
        std::mutex m;
        S acc;
      };
      auto st = std::make_shared<state>();
      st->acc = seed;
      return src.subscribe(
          [st, f](const T& v) {
            std::lock_guard<std::mutex> lock(st->m);
            st->acc = f(st->acc, v);
          },
          on_err,
          [st, on_next, on_done] {
            S out;
            {
              std::lock_guard<std::mutex> lock(st->m);
              out = st->acc;
            }
            if (on_next) on_next(out);
            if (on_done) on_done();
          });
    });
  }
};
template <class S, class F>
inline auto reduce(S seed, F f) {
  return op_reduce<S, F>{std::move(seed), std::move(f)};
}

// pairwise(): emits (previous, current) pairs starting with the second value.
struct op_pairwise {
  template <class T>
  auto operator()(const observable<T>& src) const {
    using P = std::pair<T, T>;
    return observable<P>::create([src](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::optional<T> prev;
      };
      auto st = std::make_shared<state>();
      return src.subscribe(
          [st, on_next](const T& v) {
            std::optional<P> out;
            {
              std::lock_guard<std::mutex> lock(st->m);
              if (st->prev) out.emplace(*st->prev, v);
              st->prev = v;
            }
            if (out && on_next) on_next(*out);
          },
          on_err, on_done);
    });
  }
};
inline auto pairwise() { return op_pairwise{}; }

} // namespace pulse
