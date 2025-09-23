#include <cassert>
#include <vector>
#include <utility>
#include <thread>
#include <chrono>
#include <pulse/pulse.hpp>
#include <iostream>
using namespace pulse;
using namespace std::chrono_literals;

int main() {
  thread_pool pool{1};
  // Two independent intervals, each limited by take(3)
  auto a = interval(20ms, pool) | take(3); // 0,1,2
  auto b = interval(30ms, pool) | take(3); // 0,1,2

  std::vector<std::pair<int,int>> got;
  auto sub = zip(a, b, [](int x, int y){ return std::make_pair(x, y); }).subscribe([&](std::pair<int,int> p){
    got.push_back(p);
  });

  std::this_thread::sleep_for(300ms);
  sub.reset();

  // zip produces pairs (0,0),(1,1),(2,2) in sync order
  assert((got.size() == 3));
  assert(got.front() == std::make_pair(0,0));
  assert(got.back()  == std::make_pair(2,2));

  std::cout << "[zip_interval_tests] OK\n";
  return 0;
}
