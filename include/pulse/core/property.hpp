#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/signal.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// ─────────────────────────────────────────────────────────────────────────────
// property<T>: an observable value (Qt's Q_PROPERTY + NOTIFY in one object).
//
//   pulse::property<int> count{0};
//   count.changed.connect([](int v){ std::cout << v; });
//   count = 5;                               // emits changed(5)
//   count.bind(other_observable);            // one-way binding
//   auto obs = count.as_observable();        // current value + every change
//
// set() emits only when the value actually changes (by Eq). Thread-safe.
// ─────────────────────────────────────────────────────────────────────────────
template <class T, class Eq = std::equal_to<T>>
class property {
public:
  using value_type = T;

  property() : st_(std::make_shared<state>()) {}
  property(T initial) : st_(std::make_shared<state>()) { st_->value = std::move(initial); }

  property(const property&) = delete;
  property& operator=(const property&) = delete;

  // Emitted with the new value after every change.
  signal<T> changed;

  T get() const {
    std::lock_guard<std::mutex> lock(st_->m);
    return st_->value;
  }
  T operator()() const { return get(); }
  operator T() const { return get(); }

  // Returns true if the value changed (and `changed` was emitted).
  bool set(T v) {
    {
      std::lock_guard<std::mutex> lock(st_->m);
      if (Eq{}(st_->value, v)) return false;
      st_->value = v;
    }
    changed.emit(v);
    return true;
  }

  property& operator=(T v) {
    set(std::move(v));
    return *this;
  }

  // Modify in place: p.update([](auto& v){ v.push_back(1); });
  template <class F>
  bool update(F&& f) {
    T copy = get();
    std::forward<F>(f)(copy);
    return set(std::move(copy));
  }

  // Shorthand for changed.connect(...).
  template <class F>
  connection on_change(F&& f) {
    return changed.connect(std::forward<F>(f));
  }

  // Current value + every subsequent change.
  observable<T> as_observable() const {
    std::weak_ptr<state> w = st_;
    auto changes = changed.as_observable();
    return observable<T>::create([w, changes](typename observable<T>::OnNext on_next, auto,
                                              auto) {
      auto s = w.lock();
      if (!s) return subscription{};
      auto sub = changes.subscribe(on_next);
      T cur;
      {
        std::lock_guard<std::mutex> lock(s->m);
        cur = s->value;
      }
      on_next(cur);
      return sub;
    });
  }

  // Derived stream: current value and every change, transformed by f.
  //   Text(count.map([](int c){ return "Count: " + std::to_string(c); }))
  template <class F>
  auto map(F f) const {
    using U = std::decay_t<std::invoke_result_t<F&, const T&>>;
    auto src = as_observable();
    return observable<U>::create([src, f](auto on_next, auto on_err, auto on_done) {
      return src.subscribe([f, on_next](const T& v) { on_next(f(v)); }, on_err, on_done);
    });
  }

  // One-way binding: follow the source. Replaces any previous binding.
  void bind(const observable<T>& src) {
    unbind();
    binding_ = src.subscribe([this](const T& v) { set(v); });
  }
  void bind(const property& src) { bind(src.as_observable()); }

  template <class U, class E2, class F>
  void bind(const property<U, E2>& src, F map_fn) {
    unbind();
    binding_ = src.as_observable().subscribe([this, map_fn](const U& v) { set(map_fn(v)); });
  }

  void unbind() { binding_.reset(); }
  bool is_bound() const { return binding_.active(); }

  // Two-way binding with another property (initial value taken from `other`).
  subscription bind_bidirectional(property& other) {
    set(other.get());
    auto a = changed.connect([&other](const T& v) { other.set(v); });
    auto b = other.changed.connect([this](const T& v) { set(v); });
    return subscription([a, b] {
      a.disconnect();
      b.disconnect();
    });
  }

private:
  struct state {
    mutable std::mutex m;
    T value{};
  };
  std::shared_ptr<state> st_;
  subscription binding_;
};

// ─────────────────────────────────────────────────────────────────────────────
// computed<R>: a read-only property derived from other properties.
//
//   pulse::property<int> a{1}, b{2};
//   pulse::computed sum([&]{ return a.get() + b.get(); }, a, b);
//   sum.changed.connect(...);   sum.get() == 3
//
// The sources must outlive the computed object.
// ─────────────────────────────────────────────────────────────────────────────
template <class R>
class computed {
  std::function<R()> fn_;
  property<R> value_;
  std::vector<subscription> subs_;

public:
  using value_type = R;

  template <class F, class... Sources>
  explicit computed(F f, Sources&... sources) : fn_(std::move(f)) {
    value_.set(fn_());
    (subs_.push_back(sources.changed.connect([this] { recompute(); }).scoped()), ...);
  }

  computed(const computed&) = delete;
  computed& operator=(const computed&) = delete;

  signal<R>& changed = value_.changed;

  R get() const { return value_.get(); }
  R operator()() const { return get(); }
  operator R() const { return get(); }
  observable<R> as_observable() const { return value_.as_observable(); }
  template <class F>
  auto map(F f) const { return value_.map(std::move(f)); }
  void recompute() { value_.set(fn_()); }
};

template <class F, class... S>
computed(F, S&...) -> computed<std::decay_t<std::invoke_result_t<F&>>>;

} // namespace pulse
