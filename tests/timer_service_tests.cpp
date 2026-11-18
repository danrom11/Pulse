#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <pulse/pulse.hpp>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  auto& ts = timer_service::instance();

  // ordering by due time
  {
    std::mutex m;
    std::vector<int> order;
    auto a = ts.schedule_after(60ms, [&] { std::lock_guard l(m); order.push_back(3); });
    auto b = ts.schedule_after(20ms, [&] { std::lock_guard l(m); order.push_back(1); });
    auto c = ts.schedule_after(40ms, [&] { std::lock_guard l(m); order.push_back(2); });
    std::this_thread::sleep_for(120ms);
    std::lock_guard l(m);
    assert((order == std::vector<int>{1, 2, 3}));
  }

  // cancellation
  {
    std::atomic<int> n{0};
    auto s = ts.schedule_after(30ms, [&] { ++n; });
    s.reset();
    std::this_thread::sleep_for(60ms);
    assert(n == 0);
  }

  // periodic + cancel from inside the task
  {
    std::atomic<int> n{0};
    subscription s;
    std::mutex m;
    {
      std::lock_guard l(m);
      s = ts.schedule_periodic(0ms, 10ms, [&] {
        if (++n == 3) {
          std::lock_guard l2(m);
          s.reset();
        }
      });
    }
    std::this_thread::sleep_for(120ms);
    assert(n == 3);
  }

  // post_delayed hops to the executor
  {
    strand q;
    int got = 0;
    auto s = post_delayed(q, 20ms, [&] { got = 1; });
    std::this_thread::sleep_for(50ms);
    assert(got == 0);
    q.drain();
    assert(got == 1);
  }

  // many timers do not create threads (smoke test for scale)
  {
    std::atomic<int> n{0};
    std::vector<subscription> subs;
    for (int i = 0; i < 2000; ++i) subs.push_back(ts.schedule_after(5ms, [&] { ++n; }));
    std::this_thread::sleep_for(150ms);
    assert(n == 2000);
  }

  std::cout << "[timer_service_tests] OK\n";
  return 0;
}
