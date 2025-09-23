#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <mutex>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace pulse {

// ── Connectable Observable ────────────────────────────────────────────────────
template <class T>
class connectable_observable {
public:
  using OnNext = typename observable<T>::OnNext;
  using OnErr  = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  explicit connectable_observable(observable<T> src)
    : src_(std::move(src)), hub_(std::make_shared<hub_t>()) {}

  observable<T> as_observable() const {
    auto hub = hub_;
    return observable<T>::create([hub](OnNext on_next, OnErr on_err, OnDone on_done){
      std::size_t idx = 0;
      {
        std::lock_guard<std::mutex> lock(hub->m);
        idx = hub->next_id++;
        hub->subs.push_back({idx, std::move(on_next), std::move(on_err), std::move(on_done)});
      }
      return subscription([hub, idx]{
        std::lock_guard<std::mutex> lock(hub->m);
        auto& v = hub->subs;
        v.erase(std::remove_if(v.begin(), v.end(),
                  [idx](const slot& s){ return s.id == idx; }), v.end());
      });
    });
  }

  subscription connect() const {
    bool need_start = false;
    {
      std::lock_guard<std::mutex> lock(hub_->m);
      if (!hub_->started) {
        hub_->started   = true;
        hub_->completed = false;
        hub_->errored   = false;
        hub_->err_ptr   = {};
        need_start = true;
      }
    }

    if (!need_start) {
      return subscription{};
    }

    hub_->upstream = src_.subscribe(
      [h = hub_](const T& v){
        std::vector<OnNext> local;
        {
          std::lock_guard<std::mutex> lock(h->m);
          local.reserve(h->subs.size());
          for (auto& s : h->subs) local.push_back(s.on_next);
        }
        for (auto& f : local) if (f) f(v);
      },
      [h = hub_](std::exception_ptr e){
        std::vector<OnErr> local;
        {
          std::lock_guard<std::mutex> lock(h->m);
          h->errored = true; h->err_ptr = e;
          local.reserve(h->subs.size());
          for (auto& s : h->subs) local.push_back(s.on_err);
          h->subs.clear();
        }
        for (auto& f : local) if (f) f(e);
      },
      [h = hub_]{
        std::vector<OnDone> local;
        {
          std::lock_guard<std::mutex> lock(h->m);
          h->completed = true;
          local.reserve(h->subs.size());
          for (auto& s : h->subs) local.push_back(s.on_done);
          h->subs.clear();
        }
        for (auto& f : local) if (f) f();
      }
    );

    auto hub = hub_;
    return subscription([hub]{
      std::lock_guard<std::mutex> lock(hub->m);
      hub->upstream.reset();
      hub->started = false;
    });
  }

  void disconnect() const {
    std::lock_guard<std::mutex> lock(hub_->m);
    hub_->upstream.reset();
    hub_->started   = false;
    hub_->completed = false;
    hub_->errored   = false;
    hub_->err_ptr   = {};
  }

private:
  struct slot {
    std::size_t id;
    OnNext on_next;
    OnErr  on_err;
    OnDone on_done;
  };
  struct hub_t {
    std::mutex m;
    std::vector<slot> subs;
    subscription upstream;
    bool started{false};
    bool completed{false};
    bool errored{false};
    std::exception_ptr err_ptr{};
    std::size_t next_id{0};
  };

  observable<T> src_;
  std::shared_ptr<hub_t> hub_;
};

// publish(): cold -> connectable
template <class T>
inline connectable_observable<T> publish(const observable<T>& src) {
  return connectable_observable<T>(src);
}

// ref_count(): auto start/stop based on the number of subscribers
template <class T>
inline observable<T> ref_count(connectable_observable<T> conn) {
  using OnNext = typename observable<T>::OnNext;
  using OnErr  = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  struct rc_state {
    std::mutex m;
    std::size_t refs{0};
    subscription conn_sub;
  };
  auto st  = std::make_shared<rc_state>();
  auto hot = conn.as_observable();

  return observable<T>::create([conn, hot, st](OnNext on_next, OnErr on_err, OnDone on_done){
    {
      std::lock_guard<std::mutex> lock(st->m);
      if (st->refs++ == 0) {
        st->conn_sub = conn.connect();
      }
    }

    subscription down = hot.subscribe(on_next, on_err, on_done);
    auto down_sp = std::make_shared<subscription>(std::move(down));

    return subscription([down_sp, st]{
      down_sp->reset();
      std::lock_guard<std::mutex> lock(st->m);
      if (st->refs > 0) --st->refs;
      if (st->refs == 0) {
        st->conn_sub.reset();
      }
    });
  });
}

// ref_count(conn, grace): auto start/stop with delay
template <class T, class Rep, class Period>
inline observable<T> ref_count(connectable_observable<T> conn,
                               std::chrono::duration<Rep,Period> grace) {
  using OnNext = typename observable<T>::OnNext;
  using OnErr  = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  struct rc_state {
    std::mutex m;
    std::size_t refs{0};
    subscription conn_sub;
    std::size_t gen{0};
  };

  auto st  = std::make_shared<rc_state>();
  auto hot = conn.as_observable();

  return observable<T>::create([conn, hot, st, grace](OnNext on_next, OnErr on_err, OnDone on_done){
    {
      std::lock_guard<std::mutex> lock(st->m);
      ++st->gen;
      if (st->refs == 0) {
        if (!st->conn_sub) {
          st->conn_sub = conn.connect();
        }
      }
      ++st->refs;
    }

    subscription down = hot.subscribe(on_next, on_err, on_done);
    auto down_sp = std::make_shared<subscription>(std::move(down));

    return subscription([down_sp, st, grace]{
      down_sp->reset();

      std::size_t my_gen_after_dec = 0;
      bool need_schedule = false;
      {
        std::lock_guard<std::mutex> lock(st->m);
        if (st->refs > 0) --st->refs;
        if (st->refs == 0) {
          need_schedule = true;
          my_gen_after_dec = ++st->gen;
        }
      }

      if (need_schedule) {
        auto st_local = st;
        std::thread([st_local, my_gen_after_dec, grace]{
          std::this_thread::sleep_for(grace);
          std::lock_guard<std::mutex> lock(st_local->m);
          if (st_local->refs == 0 && st_local->gen == my_gen_after_dec) {
            st_local->conn_sub.reset();
          }
        }).detach();
      }
    });
  });
}

} // namespace pulse
