#include <cassert>
#include <vector>
#include <pulse/pulse.hpp>
#include <iostream>
using namespace pulse;

int main() {
  inline_executor ui;
  topic<int> t;
  auto obs = as_observable(t, ui);

  std::vector<int> got;
  auto sub = (obs
    | distinct_until_changed()
    | take(3)
  ).subscribe([&](int v){ got.push_back(v); });

  int seq[] = {1,1,2,2,2,3,3,4};
  for (int v : seq) t.publish(v);

  assert((got == std::vector<int>{1,2,3}) && "distinct until changed + take(3) should yield 1,2,3");
  std::cout << "[distinct_and_take_tests] OK\n";
  return 0;
}
