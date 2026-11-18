#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// ── Connectable Observable ──────────────────────────────────────────────────
template <class T>
class connectable_observable {
public:
  using OnNext = typename observable<T>::OnNext;
  using OnErr = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  explicit connectable_observable(observable<T> src)
      : src_(std::move(src)), hub_(std::make_shared<hub_t>()) {}

  observable<T> as_observable() const {
    auto hub = hub_;
    return observable<T>::create([hub](OnNext on_next, OnErr on_err, OnDone on_done) {
      std::uint64_t id;
      auto alive = std::make_shared<std::atomic<bool>>(true);
      {
        std::lock_guard<std::mutex> lock(hub->m);
        id = hub->next_id++;
        hub->subs.push_back(std::make_shared<slot>(
            slot{id, std::move(on_next), std::move(on_err), std::move(on_done), alive}));
      }
      std::weak_ptr<hub_t> w = hub;
      return subscription([w, id, alive] {
        alive->store(false, std::memory_order_release);
        if (auto h = w.lock()) {
          std::lock_guard<std::mutex> lock(h->m);
          auto& v = h->subs;
          v.erase(std::remove_if(v.begin(), v.end(),
                                 [id](const std::shared_ptr<slot>& s) { return s->id == id; }),
                  v.end());
        }
      });
    });
  }

  // Starts the upstream (if not started yet). The returned subscription
  // disconnects it.
  subscription connect() const {
    std::uint64_t epoch;
    {
      std::lock_guard<std::mutex> lock(hub_->m);
      if (hub_->started) return subscription{};
      hub_->started = true;
      hub_->completed = false;
      hub_->err_ptr = {};
      epoch = ++hub_->epoch;
    }

    std::weak_ptr<hub_t> w = hub_;
    auto up = src_.subscribe(
        [w](const T& v) {
          auto h = w.lock();
          if (!h) return;
          for (auto& s : h->snapshot())
            if (s->alive->load(std::memory_order_acquire) && s->on_next) s->on_next(v);
        },
        [w](std::exception_ptr e) {
          auto h = w.lock();
          if (!h) return;
          std::vector<std::shared_ptr<slot>> local;
          {
            std::lock_guard<std::mutex> lock(h->m);
            h->err_ptr = e;
            h->started = false;
            local.swap(h->subs);
          }
          for (auto& s : local)
            if (s->alive->load(std::memory_order_acquire) && s->on_err) s->on_err(e);
        },
        [w] {
          auto h = w.lock();
          if (!h) return;
          std::vector<std::shared_ptr<slot>> local;
          {
            std::lock_guard<std::mutex> lock(h->m);
            h->completed = true;
            h->started = false;
            local.swap(h->subs);
          }
          for (auto& s : local)
            if (s->alive->load(std::memory_order_acquire) && s->on_done) s->on_done();
        });

    {
      std::lock_guard<std::mutex> lock(hub_->m);
      if (hub_->started && hub_->epoch == epoch) hub_->upstream = std::move(up);
    }
    up.reset();

    auto hub = hub_;
    return subscription([hub, epoch] { disconnect_epoch(*hub, epoch); });
  }

  void disconnect() const {
    std::uint64_t e;
    {
      std::lock_guard<std::mutex> lock(hub_->m);
      e = hub_->epoch;
    }
    disconnect_epoch(*hub_, e);
  }

  bool connected() const {
    std::lock_guard<std::mutex> lock(hub_->m);
    return hub_->started;
  }

private:
  struct slot {
    std::uint64_t id;
    OnNext on_next;
    OnErr on_err;
    OnDone on_done;
    std::shared_ptr<std::atomic<bool>> alive;
  };
  struct hub_t {
    std::mutex m;
    std::vector<std::shared_ptr<slot>> subs;
    subscription upstream;
    bool started{false};
    bool completed{false};
    std::exception_ptr err_ptr{};
    std::uint64_t next_id{1};
    std::uint64_t epoch{0};

    std::vector<std::shared_ptr<slot>> snapshot() {
      std::lock_guard<std::mutex> lock(m);
      return subs;
    }
  };

  static void disconnect_epoch(hub_t& h, std::uint64_t epoch) {
    subscription up;
    {
      std::lock_guard<std::mutex> lock(h.m);
      if (h.epoch != epoch) return;
      up = std::move(h.upstream);
      h.started = false;
    }
    up.reset();
  }

  observable<T> src_;
  std::shared_ptr<hub_t> hub_;
};

// publish(): cold -> connectable
template <class T>
inline connectable_observable<T> publish(const observable<T>& src) {
  return connectable_observable<T>(src);
}

// ref_count(): connect on the first subscriber, disconnect after the last.
template <class T>
inline observable<T> ref_count(connectable_observable<T> conn) {
  using OnNext = typename observable<T>::OnNext;
  using OnErr = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  struct rc_state {
    std::mutex m;
    std::size_t refs{0};
    subscription conn_sub;
  };
  auto st = std::make_shared<rc_state>();
  auto hot = conn.as_observable();

  return observable<T>::create([conn, hot, st](OnNext on_next, OnErr on_err, OnDone on_done) {
    subscription down = hot.subscribe(on_next, on_err, on_done);
    bool first = false;
    {
      std::lock_guard<std::mutex> lock(st->m);
      first = (st->refs++ == 0);
    }
    if (first) {
      auto c = conn.connect();
      std::lock_guard<std::mutex> lock(st->m);
      st->conn_sub = std::move(c);
    }
    auto down_sp = std::make_shared<subscription>(std::move(down));
    return subscription([down_sp, st] {
      down_sp->reset();
      subscription to_cancel;
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->refs > 0 && --st->refs == 0) to_cancel = std::move(st->conn_sub);
      }
      to_cancel.reset();
    });
  });
}

// ref_count(conn, grace): like ref_count(), but keeps the upstream alive for
// `grace` after the last subscriber left; a new subscriber within the grace
// period reuses it.
template <class T, class Rep, class Period>
inline observable<T> ref_count(connectable_observable<T> conn,
                               std::chrono::duration<Rep, Period> grace) {
  using OnNext = typename observable<T>::OnNext;
  using OnErr = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  struct rc_state {
    std::mutex m;
    std::size_t refs{0};
    subscription conn_sub;
    subscription grace_timer;
    std::uint64_t gen{0};
  };

  auto st = std::make_shared<rc_state>();
  auto hot = conn.as_observable();

  return observable<T>::create([conn, hot, st, grace](OnNext on_next, OnErr on_err,
                                                      OnDone on_done) {
    subscription down = hot.subscribe(on_next, on_err, on_done);
    bool need_connect = false;
    subscription cancelled_timer;
    {
      std::lock_guard<std::mutex> lock(st->m);
      ++st->gen;
      cancelled_timer = std::move(st->grace_timer);
      if (st->refs++ == 0 && !conn.connected()) need_connect = true;
    }
    cancelled_timer.reset();
    if (need_connect) {
      auto c = conn.connect();
      std::lock_guard<std::mutex> lock(st->m);
      if (c) st->conn_sub = std::move(c);
    }

    auto down_sp = std::make_shared<subscription>(std::move(down));
    return subscription([down_sp, st, grace] {
      down_sp->reset();
      std::uint64_t my_gen = 0;
      bool schedule = false;
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->refs > 0 && --st->refs == 0) {
          schedule = true;
          my_gen = ++st->gen;
        }
      }
      if (!schedule) return;
      std::weak_ptr<rc_state> w = st;
      auto t = timer_service::instance().schedule_after(grace, [w, my_gen] {
        auto s = w.lock();
        if (!s) return;
        subscription to_cancel;
        {
          std::lock_guard<std::mutex> lock(s->m);
          if (s->refs == 0 && s->gen == my_gen) to_cancel = std::move(s->conn_sub);
        }
        to_cancel.reset();
      });
      std::lock_guard<std::mutex> lock(st->m);
      if (st->gen == my_gen) st->grace_timer = std::move(t);
    });
  });
}

} // namespace pulse
