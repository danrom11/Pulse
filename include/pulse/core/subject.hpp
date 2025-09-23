#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <mutex>
#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>

namespace pulse {

// Subject<T>: hot source + observable<T>
// Thread-safe. Events are fan-out to all current subscribers.
template <class T>
class subject {
public:
  using OnNext = typename observable<T>::OnNext;
  using OnErr  = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  subject() = default;

  // as observable: subscription
  observable<T> as_observable() {
    return observable<T>::create([this](OnNext on_next, OnErr on_err, OnDone on_done) {
      std::size_t idx = 0;
      {
        std::lock_guard<std::mutex> lock(m_);
        // If it's already completed/error-free, we'll notify the subscriber immediately
        if (completed_) { if (on_done) on_done(); return subscription{}; }
        if (error_)     { if (on_err)  on_err(*error_); return subscription{}; }

        idx = next_id_++;
        slots_.push_back({idx, std::move(on_next), std::move(on_err), std::move(on_done)});
      }

      return subscription([this, idx]{
        std::lock_guard<std::mutex> lock(m_);
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
          if (it->id == idx) { slots_.erase(it); break; }
        }
      });
    });
  }

  // push-API
  void on_next(const T& v) {
    std::vector<OnNext> local;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (completed_ || error_) return;
      local.reserve(slots_.size());
      for (auto& s : slots_) local.push_back(s.on_next);
    }
    for (auto& f : local) if (f) f(v);
  }

  void on_error(std::exception_ptr e) {
    std::vector<OnErr> local;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (completed_ || error_) return;
      error_ = e;
      local.reserve(slots_.size());
      for (auto& s : slots_) local.push_back(s.on_err);
      slots_.clear();
    }
    for (auto& f : local) if (f) f(e);
  }

  void on_completed() {
    std::vector<OnDone> local;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (completed_ || error_) return;
      completed_ = true;
      local.reserve(slots_.size());
      for (auto& s : slots_) local.push_back(s.on_done);
      slots_.clear();
    }
    for (auto& f : local) if (f) f();
  }

private:
  struct slot {
    std::size_t id;
    OnNext on_next;
    OnErr  on_err;
    OnDone on_done;
  };

  std::mutex m_;
  std::vector<slot> slots_;
  std::size_t next_id_{0};
  bool completed_{false};
  std::optional<std::exception_ptr> error_;
};

} // namespace pulse
