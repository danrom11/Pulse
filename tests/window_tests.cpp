#include <cassert>
#include <iostream>
#include <vector>
#include <stdexcept>

#include <pulse/pulse.hpp>

using namespace pulse;

int main() {
  // 1) 1..7, window(3) => [1,2,3], [4,5,6], [7]
  {
    auto src = observable<int>::create([](auto on_next, auto, auto on_done){
      for (int i = 1; i <= 7; ++i) if (on_next) on_next(i);
      if (on_done) on_done();
      return subscription{};
    });

    std::vector<std::vector<int>> got;
    bool done = false;

    auto sub = (src | window(3)).subscribe(
      [&](observable<int> inner){
        got.emplace_back();
        auto &bucket = got.back();
        inner.subscribe([&](int v){ bucket.push_back(v); });
      },
      nullptr,
      [&]{ done = true; }
    );

    assert(done);
    assert(got.size() == 3);
    assert((got[0] == std::vector<int>({1,2,3})));
    assert((got[1] == std::vector<int>({4,5,6})));
    assert((got[2] == std::vector<int>({7})));
  }

  // 2) Upstream error - goes into the current window and out
  {
    auto src = observable<int>::create([](auto on_next, auto on_err, auto){
      if (on_next) on_next(10);
      if (on_err) on_err(std::make_exception_ptr(std::runtime_error("boom")));
      return subscription{};
    });

    std::vector<std::vector<int>> got;
    bool got_err = false;

    auto sub = (src | window(3)).subscribe(
      [&](observable<int> inner){
        got.emplace_back();
        auto &bucket = got.back();
        inner.subscribe(
          [&](int v){ bucket.push_back(v); },
          [&](std::exception_ptr){ /* ок */ }
        );
      },
      [&](std::exception_ptr){ got_err = true; }
    );

    assert(got_err);
    assert(got.size() == 1);
    assert((got[0] == std::vector<int>({10})));
  }

  // 3) Quickly unsubscribe after the first window – we don’t crash, the upstream is freed up
  {
    auto src = observable<int>::create([](auto on_next, auto, auto){
      for (int i = 1; i <= 100; ++i) if (on_next) on_next(i);
      return subscription{};
    });

    int outer_seen = 0;
    subscription sub = (src | window(5)).subscribe(
      [&](observable<int> inner){
        ++outer_seen;
        // we subscribe to the window, but as soon as we receive the first one, we unsubscribe from the outside
        if (outer_seen == 1) {
          int cnt = 0;
          auto isub = inner.subscribe([&](int){ ++cnt; });
          (void)isub;
          sub.reset(); // unsubscribe
        }
      }
    );
    // if you got here, everything is ok
  }

  std::cout << "[window_tests] OK\n";
  return 0;
}
