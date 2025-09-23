#include <cassert>
#include <vector>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;

int main() {
  inline_executor ui;
  topic<int> numbers;
  auto obs = as_observable(numbers, ui);

  std::vector<int> got;
  auto sub = (obs
    | map([](const int& x){ return x * 2; })
    | filter([](int x){ return x % 4 == 0; })
  ).subscribe([&](int v){
    got.push_back(v);
  });

  for (int i=1;i<=5;++i) numbers.publish(i);
  // x*2, then %4==0 => original even numbers: 2->4, 4->8
  assert((got == std::vector<int>{4,8}) && "Should only receive double even numbers");

  sub.reset();
  std::cout << "[observable_ops_tests] OK\n";
  return 0;
}
