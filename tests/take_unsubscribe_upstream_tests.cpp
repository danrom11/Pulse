#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

struct Probe {
  std::atomic<int> subs{0};
  std::atomic<int> unsubs{0};
  observable<int> make() {
    return observable<int>::create([this](auto on_next, auto, auto){
      ++subs;
      auto alive = std::make_shared<std::atomic<bool>>(true);
      std::thread([alive, on_next]{
        int i=0;
        while (alive->load(std::memory_order_acquire)) {
          if (on_next) on_next(i++);
          std::this_thread::sleep_for(5ms);
        }
      }).detach();
      return subscription([this, alive]{ alive->store(false, std::memory_order_release); ++unsubs; });
    });
  }
};

int main() {
  Probe p;
  auto limited = p.make() | take(5);
  std::vector<int> got;
  auto sub = limited.subscribe([&](int v){ got.push_back(v); });

  std::this_thread::sleep_for(100ms);
  // After take(5) the subscription should be completed and the upstream should be unsubscribed
  assert(got.size() == 5);
  assert(p.subs.load() == 1);
  assert(p.unsubs.load() == 1 && "After take(N) is completed, the upstream must be unsubscribed.");

  std::cout << "[take_unsubscribe_upstream_tests] OK\n";
  return 0;
}
