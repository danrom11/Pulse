#include <cassert>
#include <chrono>
#include <iostream>
#include <pulse/pulse.hpp>
#include <thread>
#include <vector>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  thread_pool pool{1};
  inline_executor ui;
  topic<int> t;

  std::vector<int> got;
  auto sub = (as_observable(t, ui) | throttle_latest(80ms, pool))
                 .subscribe([&](int v) { got.push_back(v); });

  // Burst 0..9: wait for the leading (0) and then the "closing" latest (9)
  for (int i = 0; i < 10; ++i)
    t.publish(i);
  std::this_thread::sleep_for(150ms); // > window: trailing (9) should exit

  // New burst only from 42: leading 42 will exit immediately, trailing is not required (no
  // new values)
  for (int i = 0; i < 5; ++i)
    t.publish(42);
  std::this_thread::sleep_for(120ms);

  sub.reset();

  // Expected: 0 (leading), 9 (trailing), then 42 (leading and/or trailing of
  // the second burst, depending on whether it hits an open or closed window -
  // the burst lands only ~10ms before the window boundary, so both are valid).
  assert(got.size() >= 2 && got.size() <= 4);
  assert(got[0] == 0);
  assert(got[1] == 9);
  for (std::size_t i = 2; i < got.size(); ++i) assert(got[i] == 42);
  std::cout << "[throttle_latest_tests] OK\n";
  return 0;
}
