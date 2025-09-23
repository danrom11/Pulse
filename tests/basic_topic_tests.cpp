#include <cassert>
#include <vector>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;

int main() {
  inline_executor ui;
  topic<int> bus;

  std::vector<int> got;
  auto sub = bus.subscribe(ui, priority{0}, bp_none{}, [&](const int& v){
    got.push_back(v);
  });

  bus.publish(1);
  bus.publish(2);
  bus.publish(3);

  assert((got == std::vector<int>{1,2,3}) && "Must get 1,2,3 in order");

  // Check the unsubscribe
  sub.reset();
  bus.publish(4);
  assert(got.size() == 3 && "After unsubscribing, events should not come");

  std::cout << "[basic_topic_tests] OK\n";
  return 0;
}
