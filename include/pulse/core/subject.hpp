#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

namespace detail {

// Shared fan-out hub used by subject / behavior_subject.
template <class T>
struct subject_hub {
  using OnNext = typename observable<T>::OnNext;
  using OnErr = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  struct slot {
    std::uint64_t id;
    OnNext on_next;
    OnErr on_err;
    OnDone on_done;
    std::shared_ptr<std::atomic<bool>> alive;
  };

  std::mutex m;
  std::vector<std::shared_ptr<slot>> slots;
  std::uint64_t next_id{1};
  bool completed{false};
  std::exception_ptr error;

  std::vector<std::shared_ptr<slot>> snapshot() {
    std::lock_guard<std::mutex> lock(m);
    return slots;
  }

  void remove(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(m);
    slots.erase(std::remove_if(slots.begin(), slots.end(),
                               [id](const std::shared_ptr<slot>& s) { return s->id == id; }),
                slots.end());
  }

  static subscription make_unsub(const std::shared_ptr<subject_hub>& hub, std::uint64_t id,
                                 std::shared_ptr<std::atomic<bool>> alive) {
    std::weak_ptr<subject_hub> w = hub;
    return subscription([w, id, alive] {
      alive->store(false, std::memory_order_release);
      if (auto h = w.lock()) h->remove(id);
    });
  }

  void next(const T& v) {
    for (auto& s : snapshot())
      if (s->alive->load(std::memory_order_acquire) && s->on_next) s->on_next(v);
  }

  void fail(std::exception_ptr e) {
    std::vector<std::shared_ptr<slot>> local;
    {
      std::lock_guard<std::mutex> lock(m);
      if (completed || error) return;
      error = e;
      local.swap(slots);
    }
    for (auto& s : local)
      if (s->alive->load(std::memory_order_acquire) && s->on_err) s->on_err(e);
  }

  void complete() {
    std::vector<std::shared_ptr<slot>> local;
    {
      std::lock_guard<std::mutex> lock(m);
      if (completed || error) return;
      completed = true;
      local.swap(slots);
    }
    for (auto& s : local)
      if (s->alive->load(std::memory_order_acquire) && s->on_done) s->on_done();
  }
};

} // namespace detail

// subject<T>: hot source + observable<T>. Thread-safe, fan-out to all current
// subscribers. Subscriptions and observables may outlive the subject.
template <class T>
class subject {
  using hub_t = detail::subject_hub<T>;

public:
  using value_type = T;
  using OnNext = typename observable<T>::OnNext;
  using OnErr = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  subject() : hub_(std::make_shared<hub_t>()) {}
  subject(const subject&) = delete;
  subject& operator=(const subject&) = delete;

  observable<T> as_observable() const {
    auto hub = hub_;
    return observable<T>::create([hub](OnNext on_next, OnErr on_err, OnDone on_done) {
      std::shared_ptr<typename hub_t::slot> s;
      {
        std::lock_guard<std::mutex> lock(hub->m);
        if (hub->completed) {
          if (on_done) on_done();
          return subscription{};
        }
        if (hub->error) {
          auto e = hub->error;
          if (on_err) on_err(e);
          return subscription{};
        }
        s = std::make_shared<typename hub_t::slot>(typename hub_t::slot{
            hub->next_id++, std::move(on_next), std::move(on_err), std::move(on_done),
            std::make_shared<std::atomic<bool>>(true)});
        hub->slots.push_back(s);
      }
      return hub_t::make_unsub(hub, s->id, s->alive);
    });
  }

  subscription subscribe(OnNext on_next, OnErr on_err = {}, OnDone on_done = {}) const {
    return as_observable().subscribe(std::move(on_next), std::move(on_err), std::move(on_done));
  }

  void on_next(const T& v) {
    {
      std::lock_guard<std::mutex> lock(hub_->m);
      if (hub_->completed || hub_->error) return;
    }
    hub_->next(v);
  }
  void on_error(std::exception_ptr e) { hub_->fail(std::move(e)); }
  void on_completed() { hub_->complete(); }

  void operator()(const T& v) { on_next(v); }

  bool has_observers() const {
    std::lock_guard<std::mutex> lock(hub_->m);
    return !hub_->slots.empty();
  }

private:
  std::shared_ptr<hub_t> hub_;
};

// behavior_subject<T>: like subject, but remembers the latest value and emits
// it to every new subscriber immediately.
template <class T>
class behavior_subject {
  struct hub_t : detail::subject_hub<T> {
    T value;
    explicit hub_t(T v) : value(std::move(v)) {}
  };
  using base_hub = detail::subject_hub<T>;

public:
  using value_type = T;
  using OnNext = typename observable<T>::OnNext;
  using OnErr = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  explicit behavior_subject(T initial = T{}) : hub_(std::make_shared<hub_t>(std::move(initial))) {}
  behavior_subject(const behavior_subject&) = delete;
  behavior_subject& operator=(const behavior_subject&) = delete;

  T value() const {
    std::lock_guard<std::mutex> lock(hub_->m);
    return hub_->value;
  }

  observable<T> as_observable() const {
    auto hub = hub_;
    return observable<T>::create([hub](OnNext on_next, OnErr on_err, OnDone on_done) {
      std::shared_ptr<typename base_hub::slot> s;
      T current;
      {
        std::lock_guard<std::mutex> lock(hub->m);
        if (hub->completed) {
          if (on_done) on_done();
          return subscription{};
        }
        if (hub->error) {
          auto e = hub->error;
          if (on_err) on_err(e);
          return subscription{};
        }
        s = std::make_shared<typename base_hub::slot>(typename base_hub::slot{
            hub->next_id++, on_next, std::move(on_err), std::move(on_done),
            std::make_shared<std::atomic<bool>>(true)});
        hub->slots.push_back(s);
        current = hub->value;
      }
      auto unsub = base_hub::make_unsub(std::static_pointer_cast<base_hub>(hub), s->id, s->alive);
      if (on_next) on_next(current);
      return unsub;
    });
  }

  subscription subscribe(OnNext on_next, OnErr on_err = {}, OnDone on_done = {}) const {
    return as_observable().subscribe(std::move(on_next), std::move(on_err), std::move(on_done));
  }

  void on_next(const T& v) {
    {
      std::lock_guard<std::mutex> lock(hub_->m);
      if (hub_->completed || hub_->error) return;
      hub_->value = v;
    }
    hub_->next(v);
  }
  void on_error(std::exception_ptr e) { hub_->fail(std::move(e)); }
  void on_completed() { hub_->complete(); }
  void operator()(const T& v) { on_next(v); }

private:
  std::shared_ptr<hub_t> hub_;
};

} // namespace pulse
