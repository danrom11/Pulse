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
          std::this_thread::sleep_for(15ms);
        }
      }).detach();
      return subscription([this, alive]{ alive->store(false, std::memory_order_release); ++unsubs; });
    });
  }
};

int main() {
  Probe p;
  auto shared = ref_count(publish(p.make()), 120ms); // grace 120мс

  auto s1 = shared.subscribe([](int){});
  std::this_thread::sleep_for(40ms);
  assert(p.subs.load() == 1);
  s1.reset(); // everyone unsubscribed
  // grace upstream is still alive
  std::this_thread::sleep_for(60ms);
  assert(p.unsubs.load() == 0 && "Inside grace, upstream is not yet turned off.");

  // Let's subscribe again BEFORE grace expires - upstream should be reused
  auto s2 = shared.subscribe([](int){});
  std::this_thread::sleep_for(40ms);
  assert(p.subs.load() == 1 && "There should be one upstream left (reuse in grace)");
  s2.reset();

  // Now let's wait for it to shut down
  std::this_thread::sleep_for(200ms);
  assert(p.unsubs.load() == 1 && "After grace and the final unsubscribe, upstream is disabled.");

  std::cout << "[share_grace_reuse_tests] OK\n";
  return 0;
}
