#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include <pulse/pulse.hpp>

using namespace pulse;

int main() {
  // 1) subscription outliving the subject must be safe to reset
  {
    subscription sub;
    {
      subject<int> s;
      sub = s.as_observable().subscribe([](int) {});
    }
    sub.reset();
  }

  // 2) subscription outliving the topic
  {
    subscription sub;
    {
      topic<int> t;
      sub = t.subscribe([](int) {});
    }
    sub.reset();
  }

  // 3) behavior_subject replays the current value
  {
    behavior_subject<int> bs{5};
    std::vector<int> a, b;
    auto s1 = bs.subscribe([&](int v) { a.push_back(v); });
    bs.on_next(6);
    auto s2 = bs.subscribe([&](int v) { b.push_back(v); });
    bs.on_next(7);
    assert((a == std::vector<int>{5, 6, 7}));
    assert((b == std::vector<int>{6, 7}));
    assert(bs.value() == 7);
  }

  // 4) subject: error is delivered to late subscribers
  {
    subject<int> s;
    s.on_error(std::make_exception_ptr(std::runtime_error("x")));
    bool got = false;
    s.subscribe([](int) {}, [&](std::exception_ptr) { got = true; });
    assert(got);
  }

  // 5) topic: queued deliveries are skipped after unsubscribe
  {
    topic<int> t;
    strand q;
    int got = 0;
    auto sub = t.subscribe(q, [&](int v) { got += v; });
    t.publish(1);
    sub.reset();
    q.drain();
    assert(got == 0);
  }

  // 6) topic: priority order
  {
    topic<int> t;
    std::vector<int> order;
    auto a = t.subscribe(inline_exec(), priority{0}, [&](int) { order.push_back(0); });
    auto b = t.subscribe(inline_exec(), priority{10}, [&](int) { order.push_back(10); });
    auto c = t.subscribe(inline_exec(), priority{5}, [&](int) { order.push_back(5); });
    t.publish(1);
    assert((order == std::vector<int>{10, 5, 0}));
  }

  // 7) topic: the passed backpressure policy instance is used (capacity 2)
  {
    topic<int> t;
    strand q;
    std::vector<int> got;
    auto sub = t.subscribe(q, priority{0}, bp_buffer<int>{2}, [&](int v) { got.push_back(v); });
    for (int i = 0; i < 10; ++i) t.publish(i);
    q.drain();
    assert((got == std::vector<int>{0, 1}));
  }

  // 8) topic: re-entrant publish and unsubscribe from a handler
  {
    topic<int> t;
    std::vector<int> got;
    subscription self;
    self = t.subscribe([&](int v) {
      got.push_back(v);
      if (v == 0) t.publish(1);
      if (v == 1) self.reset();
    });
    t.publish(0);
    t.publish(2);
    assert((got == std::vector<int>{0, 1}));
    assert(t.subscriber_count() == 0);
  }

  // 9) topic: concurrent publish / subscribe
  {
    topic<int> t;
    std::atomic<int> sum{0};
    auto base = t.subscribe([&](int v) { sum += v; });
    std::thread p([&] {
      for (int i = 0; i < 5000; ++i) t.publish(1);
    });
    std::thread s([&] {
      for (int i = 0; i < 500; ++i) {
        auto x = t.subscribe([](int) {});
      }
    });
    p.join();
    s.join();
    assert(sum.load() == 5000);
    assert(t.subscriber_count() == 1);
  }

  // 10) bp_latest coalesces a burst into the newest value
  {
    topic<int> t;
    strand q;
    std::vector<int> got;
    auto sub = t.subscribe(q, priority{0}, bp_latest<int>{}, [&](int v) { got.push_back(v); });
    for (int i = 0; i < 100; ++i) t.publish(i);
    q.drain();
    assert((got == std::vector<int>{99}));
  }

  // 11) subscription::cancel_on_destruct(false) detaches
  {
    subject<int> s;
    int got = 0;
    {
      auto sub = s.subscribe([&](int v) { got += v; });
      sub.cancel_on_destruct(false);
    }
    s.on_next(3);
    assert(got == 3);
  }

  std::cout << "[subject_topic_tests] OK\n";
  return 0;
}
