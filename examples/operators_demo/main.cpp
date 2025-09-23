#include <iostream>
#include <pulse/pulse.hpp>
#include <stdexcept>

using namespace pulse;

int main() {
  // === DEMO: take(3) =========================================================
  {
    topic<int> ints;
    inline_executor ui;

    auto sub =
        (as_observable(ints, ui) | take(3))
            .subscribe([](int v) { std::cout << "[TAKE3] " << v << "\n"; },
                       nullptr, [] { std::cout << "[TAKE3] done\n"; });

    // We are publishing 4 events, but only the first 3 will take place
    ints.publish(1);
    ints.publish(2);
    ints.publish(3);
    ints.publish(4); // it won't reach
  }

  // === DEMO: retry(2) ========================================================
  {
    inline_executor ui;

    int attempts = 0;
    auto flaky = observable<int>::create(
      [&](auto on_next, auto on_err, auto on_done){
        // "fall" the first two times
        if (attempts++ < 2) {
          if (on_err) on_err(std::make_exception_ptr(std::runtime_error("boom")));
          return subscription{};
        }
        // success on the 3rd attempt
        on_next(42);
        if (on_done) on_done();      // <-- important: we inform about completion
        return subscription{};
      }
    );

    auto sub = (flaky | retry(2))
      .subscribe(
        [](int v){ std::cout << "[RETRY] value=" << v << "\n"; },
        [](std::exception_ptr e){
          try { std::rethrow_exception(e); }
          catch (const std::exception& ex) {
            std::cout << "[RETRY] error: " << ex.what() << "\n";
          }
        },
        []{ std::cout << "[RETRY] done\n"; }
      );
  }


  return 0;
}
