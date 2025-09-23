#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/composite_subscription.hpp>
#include <optional>
#include <mutex>
#include <type_traits>
#include <utility>

namespace pulse {

// combine_latest(oa, ob, f): when both have the latest values,
// calls on_next(f(a, b)) on each new event from either stream.
// Completes when BOTH sources have completed. Throws an error immediately.
template <class A, class B, class F>
auto combine_latest(const observable<A>& oa, const observable<B>& ob, F f) {
  using R = std::invoke_result_t<F, const A&, const B&>;
  return observable<R>::create([oa, ob, f = std::move(f)](auto on_next, auto on_err, auto on_done){
    struct state_t {
      std::mutex m;
      std::optional<A> lastA;
      std::optional<B> lastB;
      bool doneA{false};
      bool doneB{false};
    };
    auto st = std::make_shared<state_t>();
    auto comp = std::make_shared<composite_subscription>();

    auto try_emit = [st, on_next, &f](){
      std::optional<R> out;
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->lastA && st->lastB) {
          out.emplace(f(*st->lastA, *st->lastB));
        }
      }
      if (out && on_next) on_next(*out);
    };

    auto subA = oa.subscribe(
      // on_next A
      [st, try_emit](const A& a){
        { std::lock_guard<std::mutex> lock(st->m); st->lastA = a; }
        try_emit();
      },
      // on_error
      [comp, on_err](std::exception_ptr e){
        if (on_err) on_err(e);
        comp->reset();
      },
      // on_done A
      [st, comp, on_done]{
        bool both_done = false;
        {
          std::lock_guard<std::mutex> lock(st->m);
          st->doneA = true;
          both_done = st->doneA && st->doneB;
        }
        if (both_done) { if (on_done) on_done(); comp->reset(); }
      }
    );

    auto subB = ob.subscribe(
      // on_next B
      [st, try_emit](const B& b){
        { std::lock_guard<std::mutex> lock(st->m); st->lastB = b; }
        try_emit();
      },
      // on_error
      [comp, on_err](std::exception_ptr e){
        if (on_err) on_err(e);
        comp->reset();
      },
      // on_done B
      [st, comp, on_done]{
        bool both_done = false;
        {
          std::lock_guard<std::mutex> lock(st->m);
          st->doneB = true;
          both_done = st->doneA && st->doneB;
        }
        if (both_done) { if (on_done) on_done(); comp->reset(); }
      }
    );

    comp->add(std::move(subA));
    comp->add(std::move(subB));
    return subscription([comp]{ comp->reset(); });
  });
}

} // namespace pulse
