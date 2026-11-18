// Regression tests for bugs fixed in Pulse 0.2.0.
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <pulse/pulse.hpp>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  // combine_latest used to capture the combiner by reference into the
  // (temporary) observable -> dangling reference.
  {
    subject<int> a, b;
    std::vector<std::string> got;
    subscription sub = combine_latest(a.as_observable(), b.as_observable(),
                                      [tag = std::string("v")](int x, int y) {
                                        return tag + std::to_string(x + y);
                                      })
                           .subscribe([&](const std::string& s) { got.push_back(s); });
    a.on_next(1);
    b.on_next(2);
    a.on_next(10);
    assert((got == std::vector<std::string>{"v3", "v12"}));
  }

  // zip: completion is signalled exactly once and outside the lock.
  {
    subject<int> a, b;
    int done = 0;
    std::vector<int> got;
    auto sub = zip(a.as_observable(), b.as_observable(), [](int x, int y) { return x * y; })
                   .subscribe([&](int v) { got.push_back(v); }, {}, [&] { ++done; });
    a.on_next(2);
    a.on_next(3);
    b.on_next(5);
    a.on_completed();
    b.on_next(7); // drains the queue of A -> completes
    b.on_next(9);
    b.on_completed();
    assert((got == std::vector<int>{10, 21}));
    assert(done == 1);
  }

  // window used to keep references to locals of the subscribe call: emitting
  // asynchronously after subscribe() returned was undefined behaviour.
  {
    subject<int> s;
    std::vector<std::vector<int>> got;
    std::vector<subscription> inner;
    auto sub = (s.as_observable() | window(2)).subscribe([&](observable<int> w) {
      got.emplace_back();
      auto idx = got.size() - 1;
      inner.push_back(w.subscribe([&got, idx](int v) { got[idx].push_back(v); }));
    });
    for (int i = 1; i <= 5; ++i) s.on_next(i);
    assert(got.size() == 3);
    assert((got[0] == std::vector<int>{1, 2}));
    assert((got[2] == std::vector<int>{5}));
  }

  // distinct_until_changed state was shared between subscriptions.
  {
    auto src = just(1, 1, 2);
    auto d = src | distinct_until_changed();
    std::vector<int> a, b;
    auto s1 = d.subscribe([&](int v) { a.push_back(v); });
    auto s2 = d.subscribe([&](int v) { b.push_back(v); });
    assert((a == std::vector<int>{1, 2}));
    assert((b == std::vector<int>{1, 2}));
  }

  // timeout passed only the first value through; all values must pass and
  // the timer must not fire after unsubscribe.
  {
    subject<int> s;
    std::vector<int> got;
    std::atomic<bool> errored{false};
    auto sub = (s.as_observable() | timeout(100ms))
                   .subscribe([&](int v) { got.push_back(v); },
                              [&](std::exception_ptr) { errored = true; });
    s.on_next(1);
    s.on_next(2);
    s.on_next(3);
    sub.reset();
    std::this_thread::sleep_for(150ms);
    assert((got == std::vector<int>{1, 2, 3}));
    assert(!errored);
  }

  // timeout fires as an idle timeout
  {
    subject<int> s;
    std::atomic<bool> errored{false};
    auto sub = (s.as_observable() | timeout(40ms))
                   .subscribe([](int) {}, [&](std::exception_ptr e) {
                     try {
                       std::rethrow_exception(e);
                     } catch (const timeout_error&) {
                       errored = true;
                     }
                   });
    std::this_thread::sleep_for(100ms);
    assert(errored);
  }

  // switch_map: completes only after the outer and the active inner complete;
  // unsubscribing cancels the inner.
  {
    subject<int> outer;
    subject<int> inner_src;
    bool done = false;
    std::vector<int> got;
    auto sub = (outer.as_observable() |
                switch_map([&](int) { return inner_src.as_observable(); }))
                   .subscribe([&](int v) { got.push_back(v); }, {}, [&] { done = true; });
    outer.on_next(1);
    inner_src.on_next(10);
    outer.on_completed();
    assert(!done && "outer completion alone must not complete switch_map");
    inner_src.on_next(11);
    inner_src.on_completed();
    assert(done);
    assert((got == std::vector<int>{10, 11}));
  }
  {
    subject<int> outer;
    subject<int> inner_src;
    auto sub = (outer.as_observable() |
                switch_map([&](int) { return inner_src.as_observable(); }))
                   .subscribe([](int) {});
    outer.on_next(1);
    assert(inner_src.has_observers());
    sub.reset();
    assert(!inner_src.has_observers() && "inner must be cancelled with the subscription");
  }

  // retry: resubscribes and forwards the final error; no self-reference leak.
  {
    int attempts = 0;
    auto failing = observable<int>::create([&](auto on_next, auto on_err, auto) {
      ++attempts;
      on_next(attempts);
      on_err(std::make_exception_ptr(std::runtime_error("boom")));
      return subscription{};
    });
    std::vector<int> got;
    bool errored = false;
    auto sub = (failing | retry(2)).subscribe([&](int v) { got.push_back(v); },
                                              [&](std::exception_ptr) { errored = true; });
    assert(attempts == 3);
    assert((got == std::vector<int>{1, 2, 3}));
    assert(errored);
  }

  // debounce: unsubscribe cancels the pending value; completion flushes it.
  {
    subject<int> s;
    std::atomic<int> got{0};
    auto sub = (s.as_observable() | debounce(30ms, inline_exec()))
                   .subscribe([&](int v) { got = v; });
    s.on_next(1);
    sub.reset();
    std::this_thread::sleep_for(60ms);
    assert(got == 0);
  }
  {
    subject<int> s;
    strand q;
    int got = 0;
    bool done = false;
    auto sub = (s.as_observable() | debounce(500ms, q))
                   .subscribe([&](int v) { got = v; }, {}, [&] { done = true; });
    s.on_next(4);
    s.on_completed();
    q.drain();
    assert(got == 4 && done);
  }

  // throttle must not block the executor (old version slept inside ex.post).
  {
    strand ui;
    subject<int> s;
    std::vector<int> got;
    auto sub = (s.as_observable() | throttle(50ms, ui)).subscribe([&](int v) { got.push_back(v); });
    auto t0 = std::chrono::steady_clock::now();
    s.on_next(1);
    s.on_next(2);
    ui.drain();
    auto dt = std::chrono::steady_clock::now() - t0;
    assert(dt < 40ms);
    assert((got == std::vector<int>{1}));
    std::this_thread::sleep_for(80ms);
    s.on_next(3);
    assert((got == std::vector<int>{1, 3}));
  }

  // share: original error is forwarded to late subscribers.
  {
    subject<int> s;
    auto shared = share(s.as_observable());
    auto s1 = shared.subscribe([](int) {});
    s.on_error(std::make_exception_ptr(std::logic_error("orig")));
    std::string msg;
    shared.subscribe([](int) {}, [&](std::exception_ptr e) {
      try {
        std::rethrow_exception(e);
      } catch (const std::logic_error& ex) {
        msg = ex.what();
      }
    });
    assert(msg == "orig");
  }

  // ref_count: a synchronous source reaches the first subscriber.
  {
    auto rc = ref_count(publish(just(1, 2, 3)));
    std::vector<int> got;
    auto sub = rc.subscribe([&](int v) { got.push_back(v); });
    assert((got == std::vector<int>{1, 2, 3}));
  }

  // take: no double completion when upstream completes after take finished.
  {
    subject<int> s;
    int done = 0;
    auto sub = (s.as_observable() | take(1)).subscribe([](int) {}, {}, [&] { ++done; });
    s.on_next(1);
    s.on_completed();
    assert(done == 1);
  }

  std::cout << "[regression_tests] OK\n";
  return 0;
}
