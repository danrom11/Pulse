#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <pulse/core/subscription.hpp>

namespace pulse {

// Basic executor interface
struct executor {
  virtual ~executor() = default;
  virtual void post(std::function<void()> f) = 0;
};

// Synchronous: executes immediately on the calling thread.
struct inline_executor final : executor {
  void post(std::function<void()> f) override { f(); }
};

// Process-wide inline executor instance.
inline executor& inline_exec() {
  static inline_executor ex;
  return ex;
}

// Sequential queue without its own thread; tasks run when drain() is called.
class strand final : public executor {
public:
  void post(std::function<void()> f) override {
    std::lock_guard<std::mutex> lock(m_);
    q_.push(std::move(f));
  }

  // Runs all queued tasks (including tasks posted while draining).
  // Returns the number of executed tasks.
  std::size_t drain() {
    std::size_t n = 0;
    for (;;) {
      std::function<void()> f;
      {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.empty()) break;
        f = std::move(q_.front());
        q_.pop();
      }
      if (f) f();
      ++n;
    }
    return n;
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(m_);
    return q_.empty();
  }

private:
  mutable std::mutex m_;
  std::queue<std::function<void()>> q_;
};

// ─────────────────────────────────────────────────────────────────────────────
// timer_service: one background thread that runs delayed / periodic tasks.
// All time-based operators (timer, interval, debounce, throttle, timeout,
// delay, ref_count grace) are built on top of it instead of spawning a thread
// per event. Tasks run on the timer thread: keep them short and hop to an
// executor (ex.post) for real work.
// ─────────────────────────────────────────────────────────────────────────────
class timer_service {
public:
  using clock = std::chrono::steady_clock;
  using duration = clock::duration;
  using time_point = clock::time_point;

  timer_service() : st_(std::make_shared<state>()) {
    st_->worker = std::thread([s = st_] { run(*s); });
  }

  ~timer_service() {
    {
      std::lock_guard<std::mutex> lock(st_->m);
      st_->stop = true;
    }
    st_->cv.notify_all();
    if (st_->worker.joinable()) {
      if (st_->worker.get_id() == std::this_thread::get_id())
        st_->worker.detach();
      else
        st_->worker.join();
    }
  }

  timer_service(const timer_service&) = delete;
  timer_service& operator=(const timer_service&) = delete;

  // Global instance used by the built-in operators.
  static timer_service& instance() {
    static timer_service svc;
    return svc;
  }

  subscription schedule_at(time_point when, std::function<void()> fn) {
    return add(when, duration::zero(), std::move(fn));
  }

  template <class Rep, class Period>
  subscription schedule_after(std::chrono::duration<Rep, Period> d, std::function<void()> fn) {
    return add(clock::now() + std::chrono::duration_cast<duration>(d), duration::zero(),
               std::move(fn));
  }

  // Fixed-rate periodic task. First run after `initial`, then every `period`.
  template <class R1, class P1, class R2, class P2>
  subscription schedule_periodic(std::chrono::duration<R1, P1> initial,
                                 std::chrono::duration<R2, P2> period,
                                 std::function<void()> fn) {
    auto p = std::chrono::duration_cast<duration>(period);
    if (p <= duration::zero()) p = std::chrono::milliseconds(1);
    return add(clock::now() + std::chrono::duration_cast<duration>(initial), p, std::move(fn));
  }

  std::size_t pending() const {
    std::lock_guard<std::mutex> lock(st_->m);
    return st_->by_id.size();
  }

private:
  struct task {
    std::function<void()> fn;
    duration period{};
  };
  using key = std::pair<time_point, std::uint64_t>;

  struct state {
    mutable std::mutex m;
    std::condition_variable cv;
    std::map<key, task> queue;
    std::unordered_map<std::uint64_t, time_point> by_id;
    std::unordered_set<std::uint64_t> cancelled_running;
    std::uint64_t next_id{1};
    std::uint64_t running_id{0};
    bool stop{false};
    std::thread worker;
  };

  subscription add(time_point when, duration period, std::function<void()> fn) {
    std::uint64_t id;
    {
      std::lock_guard<std::mutex> lock(st_->m);
      id = st_->next_id++;
      st_->queue.emplace(key{when, id}, task{std::move(fn), period});
      st_->by_id.emplace(id, when);
    }
    st_->cv.notify_all();
    std::weak_ptr<state> w = st_;
    return subscription([w, id] {
      auto s = w.lock();
      if (!s) return;
      std::lock_guard<std::mutex> lock(s->m);
      auto it = s->by_id.find(id);
      if (it != s->by_id.end()) {
        s->queue.erase(key{it->second, id});
        s->by_id.erase(it);
      } else if (s->running_id == id) {
        s->cancelled_running.insert(id);
      }
    });
  }

  static void run(state& s) {
    std::unique_lock<std::mutex> lock(s.m);
    while (!s.stop) {
      if (s.queue.empty()) {
        s.cv.wait(lock, [&] { return s.stop || !s.queue.empty(); });
        continue;
      }
      auto it = s.queue.begin();
      const auto when = it->first.first;
      if (clock::now() < when) {
        s.cv.wait_until(lock, when);
        continue;
      }
      const auto id = it->first.second;
      task t = std::move(it->second);
      s.queue.erase(it);
      s.by_id.erase(id);
      s.running_id = id;
      lock.unlock();
      try {
        if (t.fn) t.fn();
      } catch (...) {
      }
      lock.lock();
      s.running_id = 0;
      const bool cancelled = s.cancelled_running.erase(id) > 0;
      if (t.period > duration::zero() && !cancelled && !s.stop) {
        auto next = when + t.period;
        const auto now = clock::now();
        if (next < now) next = now; // skip missed ticks instead of bursting
        s.queue.emplace(key{next, id}, std::move(t));
        s.by_id.emplace(id, next);
      }
    }
  }

  std::shared_ptr<state> st_;
};

// Run `fn` on executor `ex` after delay `d`. Cancel with the returned subscription.
template <class Rep, class Period>
inline subscription post_delayed(executor& ex, std::chrono::duration<Rep, Period> d,
                                 std::function<void()> fn) {
  auto alive = std::make_shared<std::atomic<bool>>(true);
  auto s = timer_service::instance().schedule_after(d, [alive, &ex, fn = std::move(fn)] {
    if (!alive->load(std::memory_order_acquire)) return;
    ex.post([alive, fn] {
      if (alive->load(std::memory_order_acquire)) fn();
    });
  });
  auto sp = std::make_shared<subscription>(std::move(s));
  return subscription([alive, sp] {
    alive->store(false, std::memory_order_release);
    sp->reset();
  });
}

} // namespace pulse
