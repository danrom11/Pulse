#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <deque>
#include <optional>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <thread>
#include <chrono>

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
        //TODO:
        // Buffer full - drop (can be changed to front/back eviction)
        // You can set a drop counter here for tracing
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
  std::size_t cap_;
  bool scheduled_{false};
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
            // взять ровно N элементов
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
// If N elements are not accumulated within TimeoutMs, it resets the entire current buffer after a timeout.
template <class T, std::size_t N, std::size_t TimeoutMs>
class bp_batch_count_or_timeout_nms {
  static_assert(N > 0, "bp_batch_count_or_timeout_nms: N must be > 0");
public:
  template <class Executor, class Invoke>
  void publish(const T& v, Executor& ex, Invoke invoke) {
    bool should_flush_batch = false;
    bool should_arm_timer   = false;

    {
      std::lock_guard<std::mutex> lock(m_);
      buf_.push_back(v);

      // if we have collected exactly N, we will schedule a batch dump
      if (buf_.size() == N && !scheduled_batch_) {
        scheduled_batch_ = true;
        should_flush_batch = true;
      }

      if (!timer_armed_) {
        timer_armed_ = true;
        last_push_   = clock::now();
        should_arm_timer = true;
      } else {
        last_push_ = clock::now();
      }
    }

    if (should_flush_batch) {
      ex.post([this, inv = std::move(invoke)]() mutable { flush_batch(inv); });
    }

    if (should_arm_timer) {
      // lightweight detached timer; policy lives in shared_ptr -> safe
      std::thread([this, &ex, inv = std::move(invoke)]() mutable {
        using namespace std::chrono;
        std::this_thread::sleep_for(milliseconds(TimeoutMs));
        bool need_flush_timeout = false;
        {
          std::lock_guard<std::mutex> lock(m_);
          // If no batch was created during the sleep time and the buffer is not empty, we drain it by timeout
          if (!buf_.empty() && !scheduled_batch_) {
            scheduled_timeout_ = true;
            need_flush_timeout = true;
          }
        }
        if (need_flush_timeout) {
          ex.post([this, inv = std::move(inv)]() mutable { flush_timeout(inv); });
        }
        // unarm the timer
        {
          std::lock_guard<std::mutex> lock(m_);
          timer_armed_ = false;
          // if new elements arrived between sleep and unarm and this is the first timer -
          // the next publish will arm the timer again.
        }
      }).detach();
    }
  }

private:
  using clock = std::chrono::steady_clock;

  template <class Invoke>
  void flush_batch(Invoke& inv) {
    // we take exactly N elements and call the handler N times
    std::array<T, N> local{};
    {
      std::lock_guard<std::mutex> lock(m_);
      for (std::size_t i = 0; i < N; ++i) {
        local[i] = std::move(buf_.front());
        buf_.pop_front();
      }
      scheduled_batch_ = false;
    }
    for (std::size_t i = 0; i < N; ++i) inv(local[i]);
  }

  template <class Invoke>
  void flush_timeout(Invoke& inv) {
    // we take everything that has accumulated by the timeout
    std::deque<T> local;
    {
      std::lock_guard<std::mutex> lock(m_);
      local.swap(buf_);
      scheduled_timeout_ = false;
    }
    for (auto& x : local) inv(x);
  }

  std::mutex m_;
  std::deque<T> buf_;
  bool scheduled_batch_{false};
  bool scheduled_timeout_{false};
  bool timer_armed_{false};
  clock::time_point last_push_{};
};




} // namespace pulse
