#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
#include <pulse/pulse.hpp>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

// Auxiliary source: counts subscriptions and unsubscriptions
struct Probe {
  std::atomic<int> subs{0};
  std::atomic<int> unsubs{0};
  observable<int> make() {
    return observable<int>::create([this](auto on_next, auto, auto){
      ++subs;
      // Ping every ~20ms to see the upstream
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
  inline_executor ui;
  Probe p;
  auto cold = p.make();
  auto conn = publish(cold);            // connectable
  auto shared = ref_count(conn, 100ms); // grace = 100мс

  // The first subscriber launches upstream
  auto a = shared.subscribe([](int){ /* no-op */ });
  std::this_thread::sleep_for(60ms);
  assert(p.subs.load() == 1 && "There should be one upstream for the first subscriber.");

  // The second subscriber should not create a new upstream
  auto b = shared.subscribe([](int){ /* no-op */ });
  std::this_thread::sleep_for(60ms);
  assert(p.subs.load() == 1 && "Leave one upstream for two subscribers");

  // Let's both unsubscribe - the upstream should end after grace
  a.reset();
  b.reset();
  // immediately after unsubscribing, Upstream is still alive in Grace
  assert(p.unsubs.load() == 0 && "Immediately after unsubscribing, the upstream is not yet complete (grace)");
  std::this_thread::sleep_for(150ms);
  assert(p.unsubs.load() == 1 && "After grace, upstream must end.");

  std::cout << "[publish_refcount_tests] OK\n";
  return 0;
}
