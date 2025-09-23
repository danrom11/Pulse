#include <cassert>
#include <vector>
#include <thread>
#include <chrono>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  thread_pool pool{1};
  inline_executor ui; // for publication
  topic<int> q;
  auto qs = as_observable(q, ui);

  std::vector<int> got;
  auto sub = (qs
    | switch_map([&](int val){
        // the previous timer should be cancelled when a new val arrives
        return timer(80ms, pool) | map([=](auto){ return val; });
      })
  ).subscribe([&](int v){ got.push_back(v); });

  q.publish(1);
  std::this_thread::sleep_for(20ms);
  q.publish(2);
  std::this_thread::sleep_for(20ms);
  q.publish(3);      // only this one should fire after ~80ms
  std::this_thread::sleep_for(150ms);
  sub.reset();

  assert((got == std::vector<int>{3}) && "switch_map must override previous internal sources");
  std::cout << "[switch_map_cancel_tests] OK\n";
  return 0;
}
