#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <type_traits>
#include <utility>

#include <pulse/core/backpressure.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/subscription.hpp>

namespace pulse {

// Priority: the higher the value, the earlier the call
struct priority {
  int value{0};
};

// Method presence detector BP::publish(const T&, Executor&, Invoke)
namespace detail {
template <class BP, class T, class Exec, class Invoke>
concept has_bp_publish = requires(BP bp, const T &v, Exec &ex, Invoke inv) {
  { bp.publish(v, ex, inv) };
};
} // namespace detail

template <class T> class topic {
public:
  topic() = default;
  topic(const topic &) = delete;
  topic &operator=(const topic &) = delete;

  // Subscription: If the BP has a publish(...) method, use it; otherwise
  // ask for accept().
  template <class Fn, class BP = bp_none>
  subscription subscribe(executor &exec, priority prio, BP bp, Fn &&fn) {
    using FnT = std::decay_t<Fn>;

    Node node{};
    node.id = next_id_.fetch_add(1, std::memory_order_relaxed);
    node.order_id = order_ctr_.fetch_add(1, std::memory_order_relaxed);
    node.prio = prio.value;
    node.exec = &exec;
    node.fn = [f = FnT(std::forward<Fn>(fn))](const T &v) { f(v); };
    node.enabled = true;

    if constexpr (detail::has_bp_publish<BP, T, executor,
                                         std::function<void(const T &)>>) {
      // Put the policy in shared_ptr so that the lambda in std::function is
      // copied
      auto sp = std::make_shared<BP>(); // don't pass bp (it may be 
                                        // non-copyable/non-movable)
      node.bp_publish =
          [sp](const T &v, executor &ex,
               const std::function<void(const T &)> &inv) mutable {
            sp->publish(v, ex, inv);
          };
    } else {
      node.bp_accept = [bp = std::move(bp)]() mutable { return bp.accept(); };
    }

    // Insert by (priority desc, order_id asc)
    auto it = nodes_.begin();
    for (; it != nodes_.end(); ++it) {
      if (node.prio > it->prio)
        break;
      if (node.prio == it->prio && node.order_id < it->order_id)
        break;
    }
    it = nodes_.insert(it, std::move(node));
    const std::uint64_t my_id = it->id;

    return subscription([this, my_id] {
      for (auto &n : nodes_) {
        if (n.id == my_id) {
          n.enabled = false;
          break;
        }
      }
    });
  }

  // Publish an event
  void publish(const T &value) {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
      if (!it->enabled)
        continue;

      auto *ex = it->exec;
      auto inv = it->fn;

      if (it->bp_publish) {
        // The policy itself will decide when and what to do (coalescing, etc.)
        it->bp_publish(value, *ex, inv);
      } else {
        // Simple accept() mode: either post a handler or drop it
        if (it->bp_accept && !it->bp_accept())
          continue;
        ex->post([inv, value] { inv(value); });
      }
    }

    // Clearing disabled
    for (auto it = nodes_.begin(); it != nodes_.end();) {
      if (!it->enabled)
        it = nodes_.erase(it);
      else
        ++it;
    }
  }

private:
  struct Node {
    std::uint64_t id{};
    std::uint64_t order_id{};
    int prio{};
    executor *exec{};
    std::function<void(const T &)> fn;

    // One of two backpressure mechanisms:
    std::function<void(const T &, executor &,
                       const std::function<void(const T &)> &)>
        bp_publish{};
    std::function<bool()> bp_accept{};

    bool enabled{false};
  };

  std::list<Node> nodes_{};
  std::atomic_uint64_t next_id_{1};
  std::atomic_uint64_t order_ctr_{1};
};

} // namespace pulse
