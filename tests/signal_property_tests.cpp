#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <pulse/pulse.hpp>

using namespace pulse;

struct Counter {
  property<int> value{0};
  pulse::signal<> reset_requested;
  void bump() { value = value.get() + 1; }
};

int main() {
  // 1) signal: connect / emit / disconnect, zero-arg slots, member slots
  {
    pulse::signal<int, std::string> sig;
    int sum = 0;
    int calls0 = 0;
    auto c1 = sig.connect([&](int v, const std::string& s) { sum += v + (int)s.size(); });
    auto c2 = sig.connect([&] { ++calls0; });
    sig.emit(1, "ab");
    sig(2, "");
    assert(sum == 5);
    assert(calls0 == 2);
    c1.disconnect();
    assert(!c1.connected());
    sig.emit(10, "x");
    assert(sum == 5);
    assert(calls0 == 3);
    assert(sig.slot_count() == 1);
  }

  // 2) disconnect during emit: later slots in the same emit are not called
  {
    pulse::signal<> sig;
    int a = 0, b = 0;
    connection cb;
    auto ca = sig.connect([&] { ++a; cb.disconnect(); });
    cb = sig.connect([&] { ++b; });
    sig.emit();
    assert(a == 1 && b == 0);
  }

  // 3) re-entrant emit and connect inside a slot
  {
    pulse::signal<int> sig;
    std::vector<int> got;
    sig.connect([&](int v) {
      got.push_back(v);
      if (v < 3) sig.emit(v + 1);
    });
    sig.emit(0);
    assert((got == std::vector<int>{0, 1, 2, 3}));
  }

  // 4) connection outlives the signal
  {
    connection c;
    {
      pulse::signal<int> sig;
      c = sig.connect([](int) {});
    }
    c.disconnect(); // must not crash
  }

  // 5) scoped connection
  {
    pulse::signal<int> sig;
    int n = 0;
    {
      scoped_connection sc = sig.connect([&](int) { ++n; });
      sig.emit(1);
    }
    sig.emit(1);
    assert(n == 1);
  }

  // 6) blocked signal, forward_to, as_observable
  {
    pulse::signal<int> a, b;
    int got_b = 0;
    b.connect([&](int v) { got_b += v; });
    a.forward_to(b);
    a.emit(5);
    a.set_blocked(true);
    a.emit(5);
    a.set_blocked(false);
    assert(got_b == 5);

    int obs_sum = 0;
    auto sub = a.as_observable().subscribe([&](int v) { obs_sum += v; });
    a.emit(3);
    sub.reset();
    a.emit(3);
    assert(obs_sum == 3);

    pulse::signal<> unit_sig;
    int units = 0;
    auto s2 = unit_sig.as_observable().subscribe([&](unit) { ++units; });
    unit_sig.emit();
    assert(units == 1);
  }

  // 7) queued connection via strand
  {
    pulse::signal<int> sig;
    strand q;
    int got = 0;
    sig.connect(q, [&](int v) { got = v; });
    sig.emit(7);
    assert(got == 0);
    q.drain();
    assert(got == 7);
  }

  // 8) property: change notification only on real change
  {
    property<int> p{1};
    std::vector<int> changes;
    p.changed.connect([&](int v) { changes.push_back(v); });
    p = 1;
    p = 2;
    p.set(2);
    p = 3;
    assert((changes == std::vector<int>{2, 3}));
    assert(p.get() == 3);
    assert(p == 3);
  }

  // 9) property::as_observable emits current value first
  {
    property<std::string> name{"a"};
    std::vector<std::string> got;
    auto sub = name.as_observable().subscribe([&](const std::string& s) { got.push_back(s); });
    name = "b";
    assert((got == std::vector<std::string>{"a", "b"}));
  }

  // 10) bindings: one-way and two-way
  {
    property<int> src{1}, dst{0};
    dst.bind(src);
    assert(dst.get() == 1);
    src = 5;
    assert(dst.get() == 5);
    dst.unbind();
    src = 6;
    assert(dst.get() == 5);

    property<int> a{1}, b{2};
    auto link = a.bind_bidirectional(b);
    assert(a.get() == 2);
    a = 10;
    assert(b.get() == 10);
    b = 20;
    assert(a.get() == 20);
    link.reset();
    a = 1;
    assert(b.get() == 20);

    property<int> num{2};
    property<std::string> text;
    text.bind(num, [](int v) { return std::to_string(v * 2); });
    assert(text.get() == "4");
    num = 21;
    assert(text.get() == "42");
  }

  // 11) computed
  {
    property<int> a{1}, b{2};
    computed sum([&] { return a.get() + b.get(); }, a, b);
    int notified = 0;
    sum.changed.connect([&](int) { ++notified; });
    assert(sum.get() == 3);
    a = 10;
    assert(sum.get() == 12);
    b = 0;
    assert(sum.get() == 10);
    assert(notified == 2);
  }

  // 12) object interaction: one object's signal drives another's property
  {
    Counter c;
    property<std::string> label;
    label.bind(c.value, [](int v) { return "count=" + std::to_string(v); });
    c.reset_requested.connect([&] { c.value = 0; });
    c.bump();
    c.bump();
    assert(label.get() == "count=2");
    c.reset_requested.emit();
    assert(label.get() == "count=0");
  }

  // 13) concurrent emit + connect/disconnect
  {
    pulse::signal<int> sig;
    std::atomic<int> total{0};
    std::thread t1([&] {
      for (int i = 0; i < 2000; ++i) sig.emit(1);
    });
    std::thread t2([&] {
      for (int i = 0; i < 500; ++i) {
        auto c = sig.connect([&](int v) { total += v; });
        c.disconnect();
      }
    });
    t1.join();
    t2.join();
    assert(sig.slot_count() == 0);
  }

  std::cout << "[signal_property_tests] OK\n";
  return 0;
}
