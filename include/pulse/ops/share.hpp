#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// share(): one upstream subscription shared by many downstream subscribers.
// - the first subscriber starts the source; the last one to leave stops it;
// - completion / error are fanned out; later subscribers immediately receive
//   the same terminal event.
template <class T>
inline observable<T> share(const observable<T>& src) {
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
  struct hub_t {
    std::mutex m;
    std::vector<std::shared_ptr<slot>> subs;
    subscription upstream;
    std::uint64_t next_id{1};
    std::uint64_t epoch{0}; // incremented every time the upstream (re)starts
    bool started{false};
    bool completed{false};
    std::exception_ptr error;
  };

  auto hub = std::make_shared<hub_t>();

  return observable<T>::create([src, hub](OnNext on_next, OnErr on_err, OnDone on_done) {
    std::shared_ptr<slot> me;
    bool need_start = false;
    std::uint64_t epoch = 0;
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
      me = std::make_shared<slot>(slot{hub->next_id++, std::move(on_next), std::move(on_err),
                                       std::move(on_done),
                                       std::make_shared<std::atomic<bool>>(true)});
      hub->subs.push_back(me);
      if (!hub->started) {
        hub->started = true;
        need_start = true;
        epoch = ++hub->epoch;
      }
    }

    if (need_start) {
      std::weak_ptr<hub_t> w = hub;
      auto up = src.subscribe(
          [w](const T& v) {
            auto h = w.lock();
            if (!h) return;
            std::vector<std::shared_ptr<slot>> local;
            {
              std::lock_guard<std::mutex> lock(h->m);
              local = h->subs;
            }
            for (auto& s : local)
              if (s->alive->load(std::memory_order_acquire) && s->on_next) s->on_next(v);
          },
          [w](std::exception_ptr e) {
            auto h = w.lock();
            if (!h) return;
            std::vector<std::shared_ptr<slot>> local;
            {
              std::lock_guard<std::mutex> lock(h->m);
              h->error = e;
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
              local.swap(h->subs);
            }
            for (auto& s : local)
              if (s->alive->load(std::memory_order_acquire) && s->on_done) s->on_done();
          });
      std::lock_guard<std::mutex> lock(hub->m);
      if (hub->started && hub->epoch == epoch && !hub->completed && !hub->error)
        hub->upstream = std::move(up);
      // otherwise everybody already left (or the source finished synchronously):
      // `up` is cancelled when it goes out of scope
    }

    const auto id = me->id;
    auto alive = me->alive;
    return subscription([hub, id, alive] {
      alive->store(false, std::memory_order_release);
      subscription to_cancel;
      {
        std::lock_guard<std::mutex> lock(hub->m);
        auto& v = hub->subs;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [id](const std::shared_ptr<slot>& s) { return s->id == id; }),
                v.end());
        if (v.empty() && hub->started) {
          hub->started = false;
          to_cancel = std::move(hub->upstream);
        }
      }
      to_cancel.reset();
    });
  });
}

// pipeable form: src | share()
struct op_share {
  template <class T>
  observable<T> operator()(const observable<T>& src) const {
    return pulse::share(src);
  }
};
inline op_share share() { return {}; }

} // namespace pulse
