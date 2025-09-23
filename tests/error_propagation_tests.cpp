#include <cassert>
#include <iostream>
#include <pulse/pulse.hpp>
#include <stdexcept>

using namespace pulse;

int main() {
  // A source that emits 1 and then throws an error
  auto src = observable<int>::create([](auto on_next, auto on_error, auto) {
    if (on_next)
      on_next(1);
    if (on_error)
      on_error(std::make_exception_ptr(std::runtime_error("boom")));
    return subscription{};
  });

  int sum = 0;
  bool got_error = false;
  auto sub = src.subscribe([&](int v) { sum += v; },
                           [&](std::exception_ptr) { got_error = true; });

  sub.reset();
  assert(sum == 1 && "Only the first on_next should pass");
  assert(got_error && "on_error must be called");

  std::cout << "[error_propagation_tests] OK\n";
  return 0;
}
