#include <chrono>
#include <iostream>
#include <latch>
#include <pulse/pulse.hpp>

using namespace pulse;

int main() {
  inline_executor ui;

  // two tickers with different periods
  auto a = interval(std::chrono::milliseconds(50), ui);
  auto b = interval(std::chrono::milliseconds(80), ui);

  // zip — glue pairs
  auto zipped = zip<std::size_t, std::size_t>(a, b, [](auto x, auto y) {
    return std::to_string(x) + "," + std::to_string(y);
  });

  std::latch done{1};
  int count = 0;

  auto sub = zipped.subscribe([&](const std::string &s) {
    std::cout << "[ZIP] " << s << "\n";
    if (++count == 5) {
      done.count_down();
    }
  });

  done.wait();
  sub.reset();
  return 0;
}
