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
          std::this_thread::sleep_for(20ms);
        }
      }).detach();
      return subscription([this, alive]{ alive->store(false, std::memory_order_release); ++unsubs; });
    });
  }
};

int main() {
  Probe p;
  auto conn = publish(p.make());   // connectable
  auto shared = ref_count(conn);   // without grace

  auto a = shared.subscribe([](int){});
  auto b = shared.subscribe([](int){});
  std::this_thread::sleep_for(60ms);
  assert(p.subs.load() == 1 && "One upstream for two subscribers");

  a.reset();
  b.reset();
  // without grace - upstream should stop almost immediately
  std::this_thread::sleep_for(40ms);
  assert(p.unsubs.load() == 1 && "After all subscribers left, the upstream ended.");

  std::cout << "[refcount_no_grace_tests] OK\n";
  return 0;
}
