#include <cassert>
#include <vector>
#include <thread>
#include <chrono>
#include <pulse/pulse.hpp>
#include <pulse/core/backpressure.hpp>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  inline_executor ui;
  topic<int> t;

  // --- bp_drop(N): accepts only the first N events ---
  std::vector<int> drop_got;
  auto s1 = t.subscribe(ui, priority{0}, bp_drop{3}, [&](int v){ drop_got.push_back(v); });
  for (int i=0;i<5;++i) t.publish(i);
  assert((drop_got == std::vector<int>{0,1,2}) && "bp_drop should only accept the first N");
  s1.reset();

  // --- bp_latest<T>: for a slow subscriber, the last element of the burst should remain ---
  thread_pool pool{1};

  std::vector<int> latest_got;
  auto s2 = t.subscribe(pool, priority{0}, bp_latest<int>{}, [&](int v){
    // emulate long processing - while the handler is busy, new values ​​are collapsed
    std::this_thread::sleep_for(40ms);
    latest_got.push_back(v);
  });

  // fast burst
  for (int i=0;i<10;++i) t.publish(i);

  // let the pool “chew” the queue
  std::this_thread::sleep_for(600ms);
  s2.reset();

  // Invariants:
  // 1) at least one value has been delivered,
  // 2) the last value delivered is 9 (the final state),
  // 3) there are few values ​​delivered (typically 1–2), but definitely not all 10.
  assert(!latest_got.empty() && "At least one value must arrive");
  assert(latest_got.back() == 9 && "The final value should come last.");
  assert(latest_got.size() <= 3 && "bp_latest should shorten the sequence significantly");

  std::cout << "[backpressure_tests] OK\n";
  return 0;
}
