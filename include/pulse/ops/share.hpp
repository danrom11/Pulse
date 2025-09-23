#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <mutex>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>

namespace pulse {

// share(): one upstream → many downstreams; the upstream is active as long as there are subscribers.
// Behavior: the first subscriber starts the source; completion/error is fan-outed;
// new subscribers receive on_completed immediately after completion.
template <class T>
inline observable<T> share(const observable<T>& src) {
  using OnNext = typename observable<T>::OnNext;
  using OnErr  = typename observable<T>::OnErr;
  using OnDone = typename observable<T>::OnDone;

  struct hub_t {
    std::mutex m;
    std::vector<OnNext> ons;
    std::vector<OnErr>  oes;
    std::vector<OnDone> ods;
    subscription upstream;
    bool started{false};
    bool completed{false};
    bool errored{false};
  };

  auto hub = std::make_shared<hub_t>();

  return observable<T>::create([src, hub](OnNext on_next, OnErr on_err, OnDone on_done) {
    std::size_t my_idx = 0;
    bool need_start = false;

    {
      std::lock_guard<std::mutex> lock(hub->m);

      // If already completed, immediately complete locally
      if (hub->completed) { if (on_done) on_done(); return subscription{}; }
      if (hub->errored)  { if (on_err)  on_err(std::make_exception_ptr(std::runtime_error("shared source already errored"))); return subscription{}; }

      // Register and get your index
      my_idx = hub->ons.size();
      hub->ons.push_back(on_next);
      hub->oes.push_back(on_err);
      hub->ods.push_back(on_done);

      // If this is the first lisener, you need to launch an upstream
      if (!hub->started) {
        hub->started = true;
        need_start = true;
      }
    }

    // Launching upstream outside of the lock
    if (need_start) {
      hub->upstream = src.subscribe(
        // on_next — fan-out to all current subscribers
        [hub](const T& v){
          std::vector<OnNext> local;
          {
            std::lock_guard<std::mutex> lock(hub->m);
            local = hub->ons;
          }
          for (auto& fn : local) if (fn) fn(v);
        },
        // on_error — fan-out and closing
        [hub](std::exception_ptr e){
          std::vector<OnErr> local;
          {
            std::lock_guard<std::mutex> lock(hub->m);
            hub->errored = true;
            local.swap(hub->oes);
            hub->ons.clear();
            hub->ods.clear();
          }
          for (auto& fe : local) if (fe) fe(e);
        },
        // on_completed — fan-out and closing
        [hub]{
          std::vector<OnDone> local;
          {
            std::lock_guard<std::mutex> lock(hub->m);
            hub->completed = true;
            local.swap(hub->ods);
            hub->ons.clear();
            hub->oes.clear();
          }
          for (auto& fd : local) if (fd) fd();
        }
      );
    }

    // Let's return a subscription that removes us from the lists and, at the last moment, 
    // extinguishes the upstream
    return subscription([hub, my_idx]{
      std::lock_guard<std::mutex> lock(hub->m);
      auto erase_at = [](auto& v, std::size_t idx){
        if (idx < v.size()) v[idx] = {}; 
        // let's leave a "hole" so that the indices of the others don't float
      };
      erase_at(hub->ons, my_idx);
      erase_at(hub->oes, my_idx);
      erase_at(hub->ods, my_idx);

      // are there any other active listeners?
      bool any = false;
      for (auto& fn : hub->ons) if (fn) { any = true; break; }
      if (!any && hub->started) {
        // The last one left — we're shutting down the upstream
        hub->upstream.reset();
        hub->started = false;
        // Note: completed/errored is NOT reset—the share is resubscribed only when a new subscriber appears; 
        // if the source is completed, new subscribers will 
        // receive on_completed immediately (see the thread above).
      }
    });
  });
}

} // namespace pulse
