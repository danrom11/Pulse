#pragma once
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/composite_subscription.hpp>
#include <memory>

namespace pulse {

// retry(k): when on_error occurs, retry the original observable up to k times.
// k=0 — no retry, simply rethrows the error.
struct op_retry {
  std::size_t k;
  template <class T>
  auto operator()(const observable<T>& src) const {
    return observable<T>::create([src, k = k](auto on_next, auto on_err, auto on_done){
      auto composite = std::make_shared<composite_subscription>();
      auto attempts  = std::make_shared<std::size_t>(0);

      // recursive (via lambda) subscription with restart capability
      std::shared_ptr<std::function<void()>> start = std::make_shared<std::function<void()>>();
      *start = [=]() {
        subscription sub = src.subscribe(
          on_next,
          [=](std::exception_ptr e){
            if (*attempts < k) {
              ++*attempts;
              // restart
              (*start)();
            } else {
              if (on_err) on_err(e);
              composite->reset();
            }
          },
          [=]{
            if (on_done) on_done();
            composite->reset();
          }
        );
        composite->add(std::move(sub));
      };

      (*start)(); // first launch

      return subscription([composite]{ composite->reset(); });
    });
  }
};

inline auto retry(std::size_t k){ return op_retry{k}; }

} // namespace pulse
