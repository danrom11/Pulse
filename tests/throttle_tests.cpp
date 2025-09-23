#include <pulse/pulse.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include <cassert>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  thread_pool pool{1};
  inline_executor ui;
  topic<int> t;

  std::vector<int> got;
  auto sub = (as_observable(t, ui) | throttle(80ms, pool)).subscribe([&](int v){
    got.push_back(v);
  });

  // Burst 0..9: only the first one from the window should pass
  for (int i=0;i<10;++i) t.publish(i);
  std::this_thread::sleep_for(150ms); // > window

  // New burst - must pass first (42)
  for (int i=0;i<5;++i) t.publish(42);
  std::this_thread::sleep_for(120ms);

  sub.reset();

  // Expected: first from the first burst and first from the second burst
  assert(got.size() == 2);
  assert(got[0] == 0);
  assert(got[1] == 42);

  std::cout << "[throttle_tests] OK\n";
  return 0;
}
