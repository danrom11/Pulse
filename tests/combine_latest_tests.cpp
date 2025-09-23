#include <cassert>
#include <vector>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;

int main() {
  inline_executor ui;
  topic<int> ta, tb;
  auto a = as_observable(ta, ui);
  auto b = as_observable(tb, ui);

  std::vector<int> got;
  auto sub = combine_latest(a, b, [](int x, int y){ return x + y; })
    .subscribe([&](int v){ got.push_back(v); });

  // Nothing will come before the first pair; first, let's fill both
  ta.publish(1);   // no emission (b doesn't have a value yet)
  tb.publish(10);  // emission 1+10=11
  ta.publish(2);   // emission 2+10=12
  tb.publish(20);  // emission 2+20=22

  assert((got == std::vector<int>{11,12,22}) && "combine_latest should combine with the latest known values");
  sub.reset();
  std::cout << "[combine_latest_tests] OK\n";
  return 0;
}
