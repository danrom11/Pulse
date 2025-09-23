#include <cassert>
#include <vector>
#include <thread>
#include <chrono>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  inline_executor ui;
  topic<int> t;
  auto obs = as_observable(t, ui);

  std::vector<int> got;
  auto sub = (obs | debounce(120ms, ui)).subscribe([&](int v){ got.push_back(v); });

  // Quickly send 1,2,3 - only the last one (3) should go through after ~120ms
  t.publish(1);
  std::this_thread::sleep_for(30ms);
  t.publish(2);
  std::this_thread::sleep_for(30ms);
  t.publish(3);

  std::this_thread::sleep_for(200ms); // let's wait for more debounces

  assert(got.size() == 1 && got[0] == 3 && "Debounce should only skip the last value");

  sub.reset();
  std::cout << "[debounce_tests] OK\n";
  return 0;
}
