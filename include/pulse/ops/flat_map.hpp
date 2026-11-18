#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// flat_map(f) (a.k.a. merge_map): subscribes to f(value) for every outer
// value and merges all inner streams concurrently. Completes when the outer
// and all inner streams have completed.
template <class F>
struct op_flat_map {
  F f;

  template <class T>
  auto operator()(const observable<T>& src) const {
    using InnerObs = std::decay_t<decltype(f(std::declval<const T&>()))>;
    using U = typename InnerObs::value_type;

    return observable<U>::create([src, f = f](auto on_next, auto on_err, auto on_done) {
      struct state {
        std::mutex m;
        std::atomic<bool> alive{true};
        bool outer_done{false};
        std::uint64_t next_id{1};
        std::map<std::uint64_t, subscription> inners;
        subscription outer;
      };
      auto st = std::make_shared<state>();
      std::weak_ptr<state> w = st;

      auto stop_all = [](state& s) {
        std::map<std::uint64_t, subscription> in;
        subscription out;
        {
          std::lock_guard<std::mutex> lock(s.m);
          in.swap(s.inners);
          out = std::move(s.outer);
        }
        in.clear();
        out.reset();
      };

      auto fail = [w, on_err, stop_all](std::exception_ptr e) {
        auto s = w.lock();
        if (!s || !s->alive.exchange(false)) return;
        stop_all(*s);
        if (on_err) on_err(e);
      };

      auto maybe_complete = [w, on_done] {
        auto s = w.lock();
        if (!s) return;
        bool finish;
        {
          std::lock_guard<std::mutex> lock(s->m);
          finish = s->outer_done && s->inners.empty();
        }
        if (finish && s->alive.exchange(false) && on_done) on_done();
      };

      auto outer = src.subscribe(
          [w, f, on_next, fail, maybe_complete](const T& v) {
            auto s = w.lock();
            if (!s || !s->alive) return;
            InnerObs inner;
            try {
              inner = f(v);
            } catch (...) {
              fail(std::current_exception());
              return;
            }
            std::uint64_t id;
            {
              std::lock_guard<std::mutex> lock(s->m);
              id = s->next_id++;
              s->inners.emplace(id, subscription{}); // placeholder: counts as active
            }
            auto sub = inner.subscribe(
                [w, on_next](const U& u) {
                  auto s2 = w.lock();
                  if (s2 && s2->alive && on_next) on_next(u);
                },
                fail,
                [w, id, maybe_complete] {
                  if (auto s2 = w.lock()) {
                    std::lock_guard<std::mutex> lock(s2->m);
                    auto it = s2->inners.find(id);
                    if (it != s2->inners.end()) {
                      it->second.release();
                      s2->inners.erase(it);
                    }
                  }
                  maybe_complete();
                });
            std::lock_guard<std::mutex> lock(s->m);
            auto it = s->inners.find(id);
            if (it != s->inners.end()) it->second = std::move(sub);
          },
          fail,
          [w, maybe_complete] {
            if (auto s = w.lock()) {
              std::lock_guard<std::mutex> lock(s->m);
              s->outer_done = true;
            }
            maybe_complete();
          });
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->alive) st->outer = std::move(outer);
      }
      return subscription([st, stop_all] {
        st->alive = false;
        stop_all(*st);
      });
    });
  }
};

template <class F>
inline auto flat_map(F f) {
  return op_flat_map<F>{std::move(f)};
}
template <class F>
inline auto merge_map(F f) {
  return op_flat_map<F>{std::move(f)};
}

} // namespace pulse
