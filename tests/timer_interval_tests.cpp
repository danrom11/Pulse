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

  // timer: one signal
  std::vector<int> timer_got;
  auto s1 = (timer(60ms, pool)).subscribe([&](auto){ timer_got.push_back(1); });
  std::this_thread::sleep_for(120ms);
  s1.reset();
  assert(timer_got.size() == 1 && "The timer should work once.");

  // interval: for each subscriber the sequence is independent
  std::vector<int> a, b;
  auto src = interval(30ms, pool) | take(3);
  auto sa = src.subscribe([&](int v){ a.push_back(v); });
  auto sb = src.subscribe([&](int v){ b.push_back(v); });
  std::this_thread::sleep_for(200ms);
  sa.reset(); sb.reset();

  assert((a == std::vector<int>{0,1,2}) && "interval for A");
  assert((b == std::vector<int>{0,1,2}) && "interval for B");

  std::cout << "[timer_interval_tests] OK\n";
  return 0;
}
