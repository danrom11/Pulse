#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

namespace detail {

template <class F, class... Ts>
struct combine_latest_impl {
  using R = std::decay_t<std::invoke_result_t<F&, const Ts&...>>;
  using OnNext = typename observable<R>::OnNext;
  using OnErr = typename observable<R>::OnErr;
  using OnDone = typename observable<R>::OnDone;

  struct state {
    std::mutex m;
    std::tuple<std::optional<Ts>...> last;
    std::size_t done{0};
    std::atomic<bool> alive{true};
  };

  template <std::size_t I>
  static void subscribe_one(const std::tuple<observable<Ts>...>& sources,
                            const std::shared_ptr<state>& st, const std::shared_ptr<F>& f,
                            const std::shared_ptr<composite_subscription>& comp,
                            const OnNext& on_next, const OnErr& on_err, const OnDone& on_done) {
    using V = std::tuple_element_t<I, std::tuple<Ts...>>;
    comp->add(std::get<I>(sources).subscribe(
        [st, f, on_next](const V& v) {
          if (!st->alive) return;
          std::optional<R> out;
          {
            std::lock_guard<std::mutex> lock(st->m);
            std::get<I>(st->last) = v;
            const bool ready =
                std::apply([](auto&... o) { return (o.has_value() && ...); }, st->last);
            if (ready) out.emplace(std::apply([&](auto&... o) { return (*f)(*o...); }, st->last));
          }
          if (out && on_next) on_next(*out);
        },
        [st, comp, on_err](std::exception_ptr e) {
          if (!st->alive.exchange(false)) return;
          if (on_err) on_err(e);
          comp->reset();
        },
        [st, comp, on_done] {
          bool all = false;
          {
            std::lock_guard<std::mutex> lock(st->m);
            all = ++st->done == sizeof...(Ts);
          }
          if (all && st->alive.exchange(false)) {
            if (on_done) on_done();
            comp->reset();
          }
        }));
  }

  template <std::size_t... I>
  static void subscribe_all(std::index_sequence<I...>,
                            const std::tuple<observable<Ts>...>& sources,
                            const std::shared_ptr<state>& st, const std::shared_ptr<F>& f,
                            const std::shared_ptr<composite_subscription>& comp,
                            const OnNext& on_next, const OnErr& on_err, const OnDone& on_done) {
    (subscribe_one<I>(sources, st, f, comp, on_next, on_err, on_done), ...);
  }

  static observable<R> make(std::shared_ptr<F> f, std::tuple<observable<Ts>...> sources) {
    return observable<R>::create([f, sources](OnNext on_next, OnErr on_err, OnDone on_done) {
      auto st = std::make_shared<state>();
      auto comp = std::make_shared<composite_subscription>();
      subscribe_all(std::index_sequence_for<Ts...>{}, sources, st, f, comp, on_next, on_err,
                    on_done);
      return subscription([st, comp] {
        st->alive = false;
        comp->reset();
      });
    });
  }
};

} // namespace detail

// combine_latest(oa, ob, f): once every source has emitted, emits
// f(latest...) on each new value from any source. Completes when all sources
// complete; errors immediately.
template <class A, class B, class F>
auto combine_latest(const observable<A>& oa, const observable<B>& ob, F f) {
  return detail::combine_latest_impl<F, A, B>::make(std::make_shared<F>(std::move(f)),
                                                    std::make_tuple(oa, ob));
}

template <class A, class B, class C, class F>
auto combine_latest(const observable<A>& oa, const observable<B>& ob, const observable<C>& oc,
                    F f) {
  return detail::combine_latest_impl<F, A, B, C>::make(std::make_shared<F>(std::move(f)),
                                                       std::make_tuple(oa, ob, oc));
}

template <class A, class B, class C, class D, class F>
auto combine_latest(const observable<A>& oa, const observable<B>& ob, const observable<C>& oc,
                    const observable<D>& od, F f) {
  return detail::combine_latest_impl<F, A, B, C, D>::make(std::make_shared<F>(std::move(f)),
                                                          std::make_tuple(oa, ob, oc, od));
}

} // namespace pulse
