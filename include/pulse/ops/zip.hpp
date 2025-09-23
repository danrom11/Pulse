#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/composite_subscription.hpp>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace pulse {

// zip(oa, ob, f): buffers values ​​from A and B; as soon as a pair is found, emits f(a,b).
// Termination: when one source has completed and its queue is empty, terminate the overall stream.
template <class A, class B, class F>
auto zip(const observable<A>& oa, const observable<B>& ob, F f) {
  using R = std::invoke_result_t<F, const A&, const B&>;
  return observable<R>::create([oa, ob, f = std::move(f)](auto on_next, auto on_err, auto on_done){
    struct state_t {
      std::mutex m;
      std::deque<A> qa;
      std::deque<B> qb;
      bool doneA{false};
      bool doneB{false};
    };
    auto st = std::make_shared<state_t>();
    auto comp = std::make_shared<composite_subscription>();

    auto try_emit = [st, on_next, &f, on_done, comp](){
      std::optional<R> out;
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (!st->qa.empty() && !st->qb.empty()) {
          A a = std::move(st->qa.front()); st->qa.pop_front();
          B b = std::move(st->qb.front()); st->qb.pop_front();
          out.emplace(f(a, b));
        } else {
          // if someone has finished and no more pairs can be collected, we close
          if ((st->doneA && st->qa.empty()) || (st->doneB && st->qb.empty())) {
            if (on_done) on_done();
            comp->reset();
          }
        }
      }
      if (out && on_next) on_next(*out);
    };

    auto sa = oa.subscribe(
      [st, try_emit](const A& a){
        { std::lock_guard<std::mutex> lock(st->m); st->qa.push_back(a); }
        try_emit();
      },
      [comp, on_err](std::exception_ptr e){ if (on_err) on_err(e); comp->reset(); },
      [st, try_emit]{ { std::lock_guard<std::mutex> lock(st->m); st->doneA = true; } try_emit(); }
    );

    auto sb = ob.subscribe(
      [st, try_emit](const B& b){
        { std::lock_guard<std::mutex> lock(st->m); st->qb.push_back(b); }
        try_emit();
      },
      [comp, on_err](std::exception_ptr e){ if (on_err) on_err(e); comp->reset(); },
      [st, try_emit]{ { std::lock_guard<std::mutex> lock(st->m); st->doneB = true; } try_emit(); }
    );

    comp->add(std::move(sa));
    comp->add(std::move(sb));
    return subscription([comp]{ comp->reset(); });
  });
}

} // namespace pulse
