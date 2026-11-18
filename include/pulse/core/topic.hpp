#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulse/core/backpressure.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// Priority: the higher the value, the earlier the handler is called.
struct priority {
  int value{0};
};

namespace detail {
// Detects BP::publish(const T&, Executor&, Invoke)
template <class BP, class T, class Exec, class Invoke>
concept has_bp_publish = requires(BP bp, const T& v, Exec& ex, Invoke inv) {
  { bp.publish(v, ex, inv) };
};

template <class BP>
std::shared_ptr<BP> make_policy(BP& bp) {
  if constexpr (std::is_move_constructible_v<BP>)
    return std::make_shared<BP>(std::move(bp));
  else if constexpr (std::is_copy_constructible_v<BP>)
    return std::make_shared<BP>(bp);
  else
    return std::make_shared<BP>();
}
} // namespace detail

// topic<T>: a hot, thread-safe event bus with per-subscriber executor,
// priority and backpressure policy.
//
// - Subscriptions may outlive the topic (they hold only a weak reference).
// - After unsubscribe, already-queued deliveries are skipped.
// - publish() may be called re-entrantly from a handler and from any thread.
template <class T>
class topic {
public:
  using value_type = T;

  topic() : st_(std::make_shared<state>()) {}
  topic(const topic&) = delete;
  topic& operator=(const topic&) = delete;

  // Full form: executor + priority + backpressure policy.
  template <class Fn, class BP = bp_none>
  subscription subscribe(executor& exec, priority prio, BP bp, Fn&& fn) {
    using FnT = std::decay_t<Fn>;
    auto n = std::make_shared<node>();
    n->prio = prio.value;
    auto alive = n->alive;
    auto handler = std::make_shared<FnT>(std::forward<Fn>(fn));
    executor* ex = &exec;

    if constexpr (detail::has_bp_publish<BP, T, executor, std::function<void(const T&)>>) {
      auto policy = detail::make_policy(bp);
      n->deliver = [policy, ex, handler, alive](const T& v) {
        // The invoke wrapper owns the policy: queued work keeps it alive even
        // if the subscriber goes away in the meantime.
        std::function<void(const T&)> invoke = [policy, handler, alive](const T& x) {
          if (alive->load(std::memory_order_acquire)) (*handler)(x);
        };
        policy->publish(v, *ex, invoke);
      };
    } else {
      auto policy = detail::make_policy(bp);
      auto pm = std::make_shared<std::mutex>();
      n->deliver = [policy, pm, ex, handler, alive](const T& v) {
        {
          std::lock_guard<std::mutex> lock(*pm);
          if (!policy->accept()) return;
        }
        ex->post([handler, alive, v] {
          if (alive->load(std::memory_order_acquire)) (*handler)(v);
        });
      };
    }

    std::uint64_t id;
    {
      std::lock_guard<std::mutex> lock(st_->m);
      id = st_->next_id++;
      n->id = id;
      // keep sorted by (priority desc, insertion order asc)
      auto it = std::find_if(st_->nodes.begin(), st_->nodes.end(),
                             [&](const std::shared_ptr<node>& o) { return n->prio > o->prio; });
      st_->nodes.insert(it, n);
    }

    std::weak_ptr<state> w = st_;
    return subscription([w, id, alive] {
      alive->store(false, std::memory_order_release);
      if (auto s = w.lock()) {
        std::lock_guard<std::mutex> lock(s->m);
        auto& v = s->nodes;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [id](const std::shared_ptr<node>& n) { return n->id == id; }),
                v.end());
      }
    });
  }

  template <class Fn>
  subscription subscribe(executor& exec, Fn&& fn) {
    return subscribe(exec, priority{0}, bp_none{}, std::forward<Fn>(fn));
  }

  template <class Fn>
  subscription subscribe(executor& exec, priority prio, Fn&& fn) {
    return subscribe(exec, prio, bp_none{}, std::forward<Fn>(fn));
  }

  // Synchronous delivery on the publishing thread.
  template <class Fn, std::enable_if_t<std::is_invocable_v<Fn&, const T&>, int> = 0>
  subscription subscribe(Fn&& fn) {
    return subscribe(inline_exec(), priority{0}, bp_none{}, std::forward<Fn>(fn));
  }

  void publish(const T& value) {
    std::vector<std::shared_ptr<node>> snapshot;
    {
      std::lock_guard<std::mutex> lock(st_->m);
      snapshot = st_->nodes;
    }
    for (auto& n : snapshot) {
      if (!n->alive->load(std::memory_order_acquire)) continue;
      n->deliver(value);
    }
  }

  void operator()(const T& value) { publish(value); }

  std::size_t subscriber_count() const {
    std::lock_guard<std::mutex> lock(st_->m);
    return st_->nodes.size();
  }

private:
  struct node {
    std::uint64_t id{};
    int prio{};
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::function<void(const T&)> deliver;
  };
  struct state {
    mutable std::mutex m;
    std::vector<std::shared_ptr<node>> nodes;
    std::uint64_t next_id{1};
  };

  std::shared_ptr<state> st_;
};

} // namespace pulse
