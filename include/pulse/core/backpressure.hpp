#pragma once
#include <atomic>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <deque>
#include <thread>
#include <chrono>
#include <cstdint>
#include <memory>
#include <pulse/core/scheduler.hpp>

namespace pulse {

// ── BASIC POLICIES ───────────────────────────────────────────────────────────────

struct bp_none {
  bool accept() noexcept { return true; }
};

struct bp_drop {
  std::size_t remaining;
  explicit bp_drop(std::size_t n) : remaining(n) {}
  bool accept() noexcept {
    if (remaining == 0)
      return false;
    --remaining;
    return true;
  }
};

// ── "Take only the last" ─────────────────────────────────────────────────────────
// Coalesces sequential updates: one will be executed per burst of publishes
// call the handler with the latest value.
template <class T> class bp_latest {
public:
  template <class Executor, class Invoke>
  void publish(const T &v, Executor &ex, Invoke invoke) {
    {
      std::lock_guard<std::mutex> lock(m_);
      last_ = v;
    }
    bool expected = false;
    if (scheduled_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
      ex.post([this, inv = std::move(invoke)]() mutable {
        for (;;) {
          std::optional<T> cur;
          {
            std::lock_guard<std::mutex> lock(m_);
            cur.swap(last_);
          }
          if (!cur)
            break;
          inv(*cur);
        }
        scheduled_.store(false, std::memory_order_release);
      });
    }
  }

private:
  std::mutex m_;
  std::optional<T> last_;
  std::atomic<bool> scheduled_{false};
};

// ── "Buffer for N events" ────────────────────────────────────────────────────────
// Accumulates up to N values ​​and sequentially passes them to the handler in the executor context.
// If the buffer is full, new events are dropped.
template <class T>
class bp_buffer {
public:
  explicit bp_buffer(std::size_t capacity = 64)
  : cap_(capacity ? capacity : 1) {}
  // Copying transfers the configuration only (not the queued items).
  bp_buffer(const bp_buffer& o) : cap_(o.cap_) {}

  template <class Executor, class Invoke>
  void publish(const T& v, Executor& ex, Invoke invoke) {
    bool should_schedule = false;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (q_.size() < cap_) {
        q_.push_back(v);
        // if there was no active worker, you need to schedule one
        if (!scheduled_) { scheduled_ = true; should_schedule = true; }
      } else {
        ++dropped_; // buffer full - drop the newest value
      }
    }

    if (should_schedule) {
      ex.post([this, inv = std::move(invoke)]() mutable {
        for (;;) {
          std::optional<T> item;
          {
            std::lock_guard<std::mutex> lock(m_);
            if (q_.empty()) { scheduled_ = false; break; }
            item.emplace(std::move(q_.front()));
            q_.pop_front();
          }
          inv(*item);
        }
      });
    }
  }

  std::size_t dropped() const noexcept { return dropped_; }

private:
  std::mutex m_;
  std::deque<T> q_;
  std::size_t cap_;
  bool scheduled_{false};
  std::size_t dropped_{0};
};

// ── "Buffer for N events" (compiler knows the capacity) ──────────────────────────
template <class T, std::size_t N>
class bp_buffer_n {
  static_assert(N > 0, "bp_buffer_n capacity must be > 0");
public:
  template <class Executor, class Invoke>
  void publish(const T& v, Executor& ex, Invoke invoke) {
    bool should_schedule = false;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (q_.size() < N) {
        q_.push_back(v);
        if (!scheduled_) { scheduled_ = true; should_schedule = true; }
      } else {
        // the buffer is full - drop
      }
    }
    if (should_schedule) {
      ex.post([this, inv = std::move(invoke)]() mutable {
        for (;;) {
          std::optional<T> item;
          {
            std::lock_guard<std::mutex> lock(m_);
            if (q_.empty()) { scheduled_ = false; break; }
            item.emplace(std::move(q_.front()));
            q_.pop_front();
          }
          inv(*item);
        }
      });
    }
  }
private:
  std::mutex m_;
  std::deque<T> q_;
  bool scheduled_{false};
};

// ── "Package of N events" (like a mini-batch by quantity) ────────────────────────
// Accumulates exactly N values, then calls the handler N times in one run.
// Useful if you need to reduce the overhead of posting one item at a time.
template <class T, std::size_t N>
class bp_batch_n {
  static_assert(N > 0, "bp_batch_n N must be > 0");
public:
  template <class Executor, class Invoke>
  void publish(const T& v, Executor& ex, Invoke invoke) {
    bool should_flush = false;
    {
      std::lock_guard<std::mutex> lock(m_);
      buf_.push_back(v);
      if (buf_.size() == N && !scheduled_) {
        scheduled_ = true;
        should_flush = true;
      }
    }
    if (should_flush) {
      ex.post([this, inv = std::move(invoke)]() mutable {
        std::array<T, N> local{};
        for (;;) {
          std::size_t count = 0;
          {
            std::lock_guard<std::mutex> lock(m_);
            if (buf_.size() < N) { scheduled_ = false; break; }
            // take exactly N elements
            for (std::size_t i = 0; i < N; ++i) {
              local[i] = std::move(buf_.front());
              buf_.pop_front();
            }
            count = N;
          }
          for (std::size_t i = 0; i < count; ++i) inv(local[i]);
        }
      });
    }
  }
private:
  std::mutex m_;
  std::deque<T> buf_;
  bool scheduled_{false};
};

// Accumulates up to N elements and calls the handler N times in a single run.
// If N elements are not accumulated within TimeoutMs after the first element of
// a batch, everything accumulated so far is flushed.
template <class T, std::size_t N, std::size_t TimeoutMs>
class bp_batch_count_or_timeout_nms {
  static_assert(N > 0, "bp_batch_count_or_timeout_nms: N must be > 0");
public:
  template <class Executor, class Invoke>
  void publish(const T& v, Executor& ex, Invoke invoke) {
    bool should_flush_batch = false;
    bool should_arm_timer   = false;
    std::uint64_t gen = 0;
    {
      std::lock_guard<std::mutex> lock(st_->m);
      st_->buf.push_back(v);
      if (st_->buf.size() >= N && !st_->scheduled_batch) {
        st_->scheduled_batch = true;
        should_flush_batch = true;
      }
      if (!st_->timer_armed) {
        st_->timer_armed = true;
        should_arm_timer = true;
        gen = ++st_->gen;
      }
    }

    if (should_flush_batch) {
      ex.post([st = st_, inv = invoke]() mutable { flush(*st, inv, true); });
    }
    if (should_arm_timer) {
      std::weak_ptr<shared_state> w = st_;
      Executor* exp = &ex;
      st_->timer = timer_service::instance().schedule_after(
        std::chrono::milliseconds(TimeoutMs), [w, exp, gen, inv = invoke]() mutable {
          auto st = w.lock();
          if (!st) return;
          {
            std::lock_guard<std::mutex> lock(st->m);
            if (st->gen != gen) return;
            st->timer_armed = false;
            if (st->buf.empty()) return;
          }
          exp->post([st, inv]() mutable { flush(*st, inv, false); });
        });
    }
  }

private:
  struct shared_state {
    std::mutex m;
    std::deque<T> buf;
    bool scheduled_batch{false};
    bool timer_armed{false};
    std::uint64_t gen{0};
    subscription timer;
  };

  template <class Invoke>
  static void flush(shared_state& st, Invoke& inv, bool full_batches_only) {
    std::deque<T> local;
    {
      std::lock_guard<std::mutex> lock(st.m);
      if (full_batches_only) {
        while (st.buf.size() >= N) {
          for (std::size_t i = 0; i < N; ++i) {
            local.push_back(std::move(st.buf.front()));
            st.buf.pop_front();
          }
        }
        st.scheduled_batch = false;
        if (st.buf.empty()) { st.timer_armed = false; ++st.gen; }
      } else {
        local.swap(st.buf);
      }
    }
    for (auto& x : local) inv(x);
  }

  std::shared_ptr<shared_state> st_ = std::make_shared<shared_state>();
};

} // namespace pulse
