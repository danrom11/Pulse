#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <pulse/pulse.hpp>

using namespace pulse;

static observable<int> make_src_range(int from, int to) {
  return observable<int>::create([=](auto on_next, auto, auto on_done) {
    for (int i = from; i <= to; ++i) {
      if (on_next)
        on_next(i);
    }
    if (on_done)
      on_done();
    return subscription{};
  });
}

int main() {
  // 1) grouping and throwing of the tail
  {
    auto src = make_src_range(1, 5);
    std::vector<std::vector<int>> got;

    auto sub =
        (src | buffer(2))
            .subscribe([&](const std::vector<int> &v) { got.push_back(v); },
                       [&](std::exception_ptr) {
                         assert(false && "no errors expected");
                       });

    // expected: [1,2], [3,4], [5]
    assert(got.size() == 3);
    assert((got[0] == std::vector<int>({1, 2})));
    assert((got[1] == std::vector<int>({3, 4})));
    assert((got[2] == std::vector<int>({5})));
  }

  // 2) the error should not emit a tail
  {
    auto src = observable<int>::create([](auto on_next, auto on_err, auto) {
      if (on_next)
        on_next(1);
      if (on_next)
        on_next(2);
      if (on_err)
        on_err(std::make_exception_ptr(std::runtime_error("boom")));
      return subscription{};
    });

    std::vector<std::vector<int>> got;
    bool got_error = false;

    auto sub =
        (src | buffer(3))
            .subscribe([&](const std::vector<int> &v) { got.push_back(v); },
                       [&](std::exception_ptr) { got_error = true; });

    assert(got_error && "must receive error");
    assert(got.empty() && "tail must NOT be flushed on error");
  }

  // 3) exact multiples
  {
    auto src = make_src_range(1, 6);
    std::vector<std::vector<int>> got;

    auto sub =
        (src | buffer(3))
            .subscribe([&](const std::vector<int> &v) { got.push_back(v); },
                       [&](std::exception_ptr) {
                         assert(false && "no errors expected");
                       });

    assert(got.size() == 2);
    assert((got[0] == std::vector<int>({1, 2, 3})));
    assert((got[1] == std::vector<int>({4, 5, 6})));
  }

  // 4) count==0 — exception
  {
    bool threw = false;
    auto src = make_src_range(1, 3);
    try {
      auto sub = (src | buffer(0)).subscribe([](auto &&) {});
      (void)sub;
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    assert(threw && "buffer(0) must throw std::invalid_argument");
  }

  std::cout << "[buffer_tests] OK\n";
  return 0;
}
