#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// zip(oa, ob, f): pairs values by index and emits f(a, b).
// Completes when one source has completed and its queue is drained.
template <class A, class B, class F>
auto zip(const observable<A>& oa, const observable<B>& ob, F f) {
  using R = std::decay_t<std::invoke_result_t<F&, const A&, const B&>>;
  auto fn = std::make_shared<F>(std::move(f));
  return observable<R>::create([oa, ob, fn](auto on_next, auto on_err, auto on_done) {
    struct state_t {
      std::mutex m;
      std::deque<A> qa;
      std::deque<B> qb;
      bool doneA{false};
      bool doneB{false};
      std::atomic<bool> alive{true};
    };
    auto st = std::make_shared<state_t>();
    auto comp = std::make_shared<composite_subscription>();

    // Emits every available pair, then completes if no more pairs are possible.
    auto pump = [st, fn, on_next, on_done, comp] {
      for (;;) {
        std::optional<R> out;
        bool finish = false;
        {
          std::lock_guard<std::mutex> lock(st->m);
          if (!st->qa.empty() && !st->qb.empty()) {
            A a = std::move(st->qa.front());
            st->qa.pop_front();
            B b = std::move(st->qb.front());
            st->qb.pop_front();
            out.emplace((*fn)(a, b));
          } else {
            finish = (st->doneA && st->qa.empty()) || (st->doneB && st->qb.empty());
          }
        }
        if (out) {
          if (st->alive && on_next) on_next(*out);
          continue;
        }
        if (finish && st->alive.exchange(false)) {
          if (on_done) on_done();
          comp->reset();
        }
        return;
      }
    };

    auto fail = [st, comp, on_err](std::exception_ptr e) {
      if (!st->alive.exchange(false)) return;
      if (on_err) on_err(e);
      comp->reset();
    };

    comp->add(oa.subscribe(
        [st, pump](const A& a) {
          if (!st->alive) return;
          {
            std::lock_guard<std::mutex> lock(st->m);
            st->qa.push_back(a);
          }
          pump();
        },
        fail,
        [st, pump] {
          {
            std::lock_guard<std::mutex> lock(st->m);
            st->doneA = true;
          }
          pump();
        }));

    comp->add(ob.subscribe(
        [st, pump](const B& b) {
          if (!st->alive) return;
          {
            std::lock_guard<std::mutex> lock(st->m);
            st->qb.push_back(b);
          }
          pump();
        },
        fail,
        [st, pump] {
          {
            std::lock_guard<std::mutex> lock(st->m);
            st->doneB = true;
          }
          pump();
        }));

    return subscription([st, comp] {
      st->alive = false;
      comp->reset();
    });
  });
}

} // namespace pulse
