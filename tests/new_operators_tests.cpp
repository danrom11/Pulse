#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <pulse/pulse.hpp>

using namespace pulse;
using namespace std::chrono_literals;

template <class T>
static std::vector<T> collect(const observable<T>& o, bool* done = nullptr) {
  std::vector<T> out;
  auto sub = o.subscribe([&](const T& v) { out.push_back(v); }, {}, [&] {
    if (done) *done = true;
  });
  return out;
}

int main() {
  // sources
  assert((collect(just(1, 2, 3)) == std::vector<int>{1, 2, 3}));
  assert((collect(from(std::vector<int>{4, 5})) == std::vector<int>{4, 5}));
  assert((collect(range(3, 3)) == std::vector<int>{3, 4, 5}));
  {
    bool done = false;
    assert(collect(empty<int>(), &done).empty());
    assert(done);
    bool err = false;
    error<int>(std::runtime_error("e")).subscribe([](int) {}, [&](std::exception_ptr) { err = true; });
    assert(err);
    int made = 0;
    auto d = defer([&] {
      ++made;
      return just(made);
    });
    assert((collect(d) == std::vector<int>{1}));
    assert((collect(d) == std::vector<int>{2}));
  }

  // tap / scan / reduce / pairwise
  {
    int seen = 0;
    auto o = just(1, 2, 3) | tap([&](int) { ++seen; });
    assert((collect(o) == std::vector<int>{1, 2, 3}) && seen == 3);
    assert((collect(just(1, 2, 3) | scan(0, [](int a, int v) { return a + v; })) ==
            std::vector<int>{1, 3, 6}));
    assert((collect(just(1, 2, 3, 4) | reduce(0, [](int a, int v) { return a + v; })) ==
            std::vector<int>{10}));
    auto pw = collect(just(1, 2, 3) | pairwise());
    assert(pw.size() == 2 && pw[0] == std::make_pair(1, 2) && pw[1] == std::make_pair(2, 3));
  }

  // skip / skip_while / take_while / first / last
  {
    assert((collect(range(0, 5) | skip(3)) == std::vector<int>{3, 4}));
    assert((collect(just(1, 2, 5, 1) | skip_while([](int v) { return v < 3; })) ==
            std::vector<int>{5, 1}));
    bool done = false;
    assert((collect(just(1, 2, 5, 1) | take_while([](int v) { return v < 3; }), &done) ==
            std::vector<int>{1, 2}));
    assert(done);
    assert((collect(just(7, 8) | first()) == std::vector<int>{7}));
    assert((collect(just(7, 8) | last()) == std::vector<int>{8}));
    bool err = false;
    (empty<int>() | first()).subscribe([](int) {}, [&](std::exception_ptr) { err = true; });
    assert(err);
  }

  // take_until
  {
    subject<int> src;
    subject<unit> stop;
    std::vector<int> got;
    bool done = false;
    auto sub = (src.as_observable() | take_until(stop.as_observable()))
                   .subscribe([&](int v) { got.push_back(v); }, {}, [&] { done = true; });
    src.on_next(1);
    stop.on_next(unit{});
    src.on_next(2);
    assert((got == std::vector<int>{1}) && done);
  }

  // catch_error / on_error_return / default_if_empty / finally_do
  {
    auto failing = observable<int>::create([](auto on_next, auto on_err, auto) {
      on_next(1);
      on_err(std::make_exception_ptr(std::runtime_error("x")));
      return subscription{};
    });
    assert((collect(failing | catch_error([](std::exception_ptr) { return just(9, 10); })) ==
            std::vector<int>{1, 9, 10}));
    assert((collect(failing | on_error_return(-1)) == std::vector<int>{1, -1}));
    assert((collect(empty<int>() | default_if_empty(42)) == std::vector<int>{42}));
    int fin = 0;
    {
      subject<int> s;
      auto sub = (s.as_observable() | finally_do([&] { ++fin; })).subscribe([](int) {});
      sub.reset();
      sub.reset();
    }
    assert(fin == 1);
  }

  // with_latest_from
  {
    subject<int> clicks;
    subject<std::string> text;
    std::vector<std::string> got;
    auto sub = (clicks.as_observable() |
                with_latest_from(text.as_observable(),
                                 [](int n, const std::string& t) { return t + std::to_string(n); }))
                   .subscribe([&](const std::string& s) { got.push_back(s); });
    clicks.on_next(1); // ignored: no text yet
    text.on_next("a");
    clicks.on_next(2);
    text.on_next("b");
    text.on_next("c");
    clicks.on_next(3);
    assert((got == std::vector<std::string>{"a2", "c3"}));
  }

  // flat_map merges inner streams and completes when all are done
  {
    subject<int> outer;
    subject<int> in1, in2;
    std::vector<int> got;
    bool done = false;
    auto sub = (outer.as_observable() | flat_map([&](int v) {
                  return v == 1 ? in1.as_observable() : in2.as_observable();
                }))
                   .subscribe([&](int v) { got.push_back(v); }, {}, [&] { done = true; });
    outer.on_next(1);
    outer.on_next(2);
    in1.on_next(10);
    in2.on_next(20);
    in1.on_next(11);
    outer.on_completed();
    in1.on_completed();
    assert(!done);
    in2.on_completed();
    assert(done);
    assert((got == std::vector<int>{10, 20, 11}));
  }

  // combine_latest with three sources
  {
    subject<int> a, b, c;
    std::vector<int> got;
    auto sub = combine_latest(a.as_observable(), b.as_observable(), c.as_observable(),
                              [](int x, int y, int z) { return x + y + z; })
                   .subscribe([&](int v) { got.push_back(v); });
    a.on_next(1);
    b.on_next(2);
    c.on_next(3);
    a.on_next(10);
    assert((got == std::vector<int>{6, 15}));
  }

  // delay
  {
    strand q;
    subject<int> s;
    std::vector<int> got;
    auto sub = (s.as_observable() | delay(30ms, q)).subscribe([&](int v) { got.push_back(v); });
    s.on_next(1);
    q.drain();
    assert(got.empty());
    std::this_thread::sleep_for(60ms);
    q.drain();
    assert((got == std::vector<int>{1}));
  }

  // distinct (all-time) and distinct_until_changed with key
  {
    assert((collect(just(1, 2, 1, 3, 2) | distinct()) == std::vector<int>{1, 2, 3}));
    assert((collect(just(std::string("a"), std::string("b"), std::string("bb"), std::string("c")) |
                    distinct_until_changed([](const std::string& s) { return s.size(); }))
                .size() == 3));
  }

  // pipeable share()
  {
    int subs = 0;
    auto src = observable<int>::create([&](auto, auto, auto) {
      ++subs;
      return subscription{};
    });
    auto shared = src | share();
    auto a = shared.subscribe([](int) {});
    auto b = shared.subscribe([](int) {});
    assert(subs == 1);
  }

  std::cout << "[new_operators_tests] OK\n";
  return 0;
}
