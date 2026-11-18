#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// ─────────────────────────────────────────────────────────────────────────────
// connection: a lightweight, copyable handle to a signal slot (like
// QMetaObject::Connection). It does NOT disconnect on destruction - use
// scoped() / scoped_connection when you want RAII.
// ─────────────────────────────────────────────────────────────────────────────
class connection {
public:
  connection() = default;
  explicit connection(std::function<void()> disconnect, std::shared_ptr<std::atomic<bool>> alive)
      : disconnect_(std::make_shared<std::function<void()>>(std::move(disconnect))),
        alive_(std::move(alive)) {}

  void disconnect() const {
    if (disconnect_ && *disconnect_) {
      auto fn = std::move(*disconnect_);
      *disconnect_ = nullptr;
      fn();
    }
  }

  bool connected() const { return alive_ && alive_->load(std::memory_order_acquire); }
  explicit operator bool() const { return connected(); }

  // Turn into an RAII subscription (disconnects when destroyed).
  subscription scoped() const {
    auto self = *this;
    return subscription([self] { self.disconnect(); });
  }

  operator subscription() const { return scoped(); }

private:
  std::shared_ptr<std::function<void()>> disconnect_;
  std::shared_ptr<std::atomic<bool>> alive_;
};

// RAII connection holder.
class scoped_connection {
public:
  scoped_connection() = default;
  scoped_connection(connection c) : c_(std::move(c)) {}
  scoped_connection(scoped_connection&&) noexcept = default;
  scoped_connection& operator=(scoped_connection&& o) noexcept {
    if (this != &o) {
      c_.disconnect();
      c_ = std::move(o.c_);
      o.c_ = connection{};
    }
    return *this;
  }
  scoped_connection& operator=(connection c) {
    c_.disconnect();
    c_ = std::move(c);
    return *this;
  }
  scoped_connection(const scoped_connection&) = delete;
  scoped_connection& operator=(const scoped_connection&) = delete;
  ~scoped_connection() { c_.disconnect(); }

  void disconnect() { c_.disconnect(); }
  connection release() {
    auto c = c_;
    c_ = connection{};
    return c;
  }
  bool connected() const { return c_.connected(); }

private:
  connection c_;
};

namespace detail {
template <class T>
struct signal_value {
  using type = std::tuple<std::decay_t<T>>;
};
} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// signal<Args...>: synchronous multicast callback list (Qt-like signals).
//
//   pulse::signal<int> value_changed;
//   value_changed.connect([](int v){ ... });   // or connect([]{ ... })
//   value_changed.emit(42);                     // or value_changed(42)
//
// - Thread-safe: connect / disconnect / emit from any thread.
// - Re-entrant: slots may connect, disconnect or emit while being invoked.
// - A slot disconnected during an emit is not called afterwards.
// - Connections may outlive the signal.
// ─────────────────────────────────────────────────────────────────────────────
template <class... Args>
class signal {
public:
  using slot_fn = std::function<void(const Args&...)>;

  signal() : st_(std::make_shared<state>()) {}
  signal(const signal&) = delete;
  signal& operator=(const signal&) = delete;
  // Moving transfers all connections; the moved-from signal is empty but usable.
  signal(signal&& o) : st_(std::move(o.st_)) { o.st_ = std::make_shared<state>(); }
  signal& operator=(signal&& o) {
    if (this != &o) {
      st_ = std::move(o.st_);
      o.st_ = std::make_shared<state>();
    }
    return *this;
  }

  // Connect a callable taking (Args...) or no arguments.
  template <class F>
  connection connect(F&& f) {
    slot_fn fn;
    if constexpr (std::is_invocable_v<F&, const Args&...>) {
      fn = slot_fn(std::forward<F>(f));
    } else {
      static_assert(std::is_invocable_v<F&>,
                    "pulse::signal::connect: slot must accept the signal arguments or nothing");
      fn = [g = std::decay_t<F>(std::forward<F>(f))](const Args&...) mutable { g(); };
    }
    return add(std::move(fn), nullptr);
  }

  // Connect with delivery on an executor (queued connection, like Qt::QueuedConnection).
  template <class F>
  connection connect(executor& ex, F&& f) {
    slot_fn fn;
    if constexpr (std::is_invocable_v<F&, const Args&...>) {
      fn = slot_fn(std::forward<F>(f));
    } else {
      fn = [g = std::decay_t<F>(std::forward<F>(f))](const Args&...) mutable { g(); };
    }
    return add(std::move(fn), &ex);
  }

  // Connect a member function of an object.
  template <class Obj, class R, class... P>
  connection connect(Obj* obj, R (Obj::*method)(P...)) {
    return connect([obj, method](const Args&... a) { (obj->*method)(a...); });
  }

  // Forward to another signal with the same signature (signal chaining).
  connection forward_to(signal& other) {
    std::weak_ptr<state> w = other.st_;
    return connect([w](const Args&... a) {
      if (auto s = w.lock()) emit_state(*s, a...);
    });
  }

  void emit(const Args&... args) const { emit_state(*st_, args...); }
  void operator()(const Args&... args) const { emit(args...); }

  void disconnect_all() {
    std::vector<std::shared_ptr<slot>> local;
    {
      std::lock_guard<std::mutex> lock(st_->m);
      local.swap(st_->slots);
    }
    for (auto& s : local) s->alive->store(false, std::memory_order_release);
  }

  std::size_t slot_count() const {
    std::lock_guard<std::mutex> lock(st_->m);
    return st_->slots.size();
  }

  // Block / unblock emission (like QObject::blockSignals).
  void set_blocked(bool b) { st_->blocked.store(b, std::memory_order_release); }
  bool blocked() const { return st_->blocked.load(std::memory_order_acquire); }

  // Observable view: signal<> -> observable<unit>, signal<T> -> observable<T>,
  // signal<A, B, ...> -> observable<std::tuple<A, B, ...>>.
  auto as_observable() const {
    using V = value_t;
    std::weak_ptr<state> w = st_;
    return observable<V>::create([w](typename observable<V>::OnNext on_next, auto, auto) {
      auto s = w.lock();
      if (!s) return subscription{};
      auto c = add_to(s, [on_next](const Args&... a) {
        if constexpr (sizeof...(Args) == 0)
          on_next(unit{});
        else if constexpr (sizeof...(Args) == 1)
          on_next(a...);
        else
          on_next(V(a...));
      }, nullptr);
      return c.scoped();
    });
  }

private:
  using value_t = std::conditional_t<
      sizeof...(Args) == 0, unit,
      std::conditional_t<sizeof...(Args) == 1,
                         std::decay_t<std::tuple_element_t<0, std::tuple<Args..., unit>>>,
                         std::tuple<std::decay_t<Args>...>>>;

  struct slot {
    std::uint64_t id;
    slot_fn fn;
    executor* ex;
    std::shared_ptr<std::atomic<bool>> alive;
  };
  struct state {
    mutable std::mutex m;
    std::vector<std::shared_ptr<slot>> slots;
    std::uint64_t next_id{1};
    std::atomic<bool> blocked{false};
  };

  connection add(slot_fn fn, executor* ex) { return add_to(st_, std::move(fn), ex); }

  static connection add_to(const std::shared_ptr<state>& st, slot_fn fn, executor* ex) {
    auto s = std::make_shared<slot>();
    s->fn = std::move(fn);
    s->ex = ex;
    s->alive = std::make_shared<std::atomic<bool>>(true);
    {
      std::lock_guard<std::mutex> lock(st->m);
      s->id = st->next_id++;
      st->slots.push_back(s);
    }
    std::weak_ptr<state> w = st;
    const auto id = s->id;
    auto alive = s->alive;
    return connection(
        [w, id, alive] {
          alive->store(false, std::memory_order_release);
          if (auto st2 = w.lock()) {
            std::lock_guard<std::mutex> lock(st2->m);
            auto& v = st2->slots;
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [id](const std::shared_ptr<slot>& x) { return x->id == id; }),
                    v.end());
          }
        },
        alive);
  }

  static void emit_state(state& st, const Args&... args) {
    if (st.blocked.load(std::memory_order_acquire)) return;
    std::vector<std::shared_ptr<slot>> snapshot;
    {
      std::lock_guard<std::mutex> lock(st.m);
      snapshot = st.slots;
    }
    for (auto& s : snapshot) {
      if (!s->alive->load(std::memory_order_acquire)) continue;
      if (s->ex) {
        auto sp = s;
        s->ex->post([sp, tup = std::tuple<std::decay_t<Args>...>(args...)] {
          if (sp->alive->load(std::memory_order_acquire)) std::apply(sp->fn, tup);
        });
      } else {
        s->fn(args...);
      }
    }
  }

  std::shared_ptr<state> st_;
};

// Connect an observable to a signal: every value is emitted through the signal.
// The signal must outlive the returned subscription.
template <class T>
subscription emit_into(const observable<T>& src, signal<T>& sig) {
  return src.subscribe([&sig](const T& v) { sig.emit(v); });
}

} // namespace pulse
