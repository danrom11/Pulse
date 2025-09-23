#include <pulse/pulse.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

int main() {
  strand io;
  thread_pool pool{1}; // the handler must be executed in the pool

  std::atomic<bool> running{true};
  std::thread io_worker([&]{
    while (running.load(std::memory_order_relaxed)) {
      io.drain();
      std::this_thread::sleep_for(1ms);
    }
    io.drain();
  });

  std::thread::id upstream_id{};
  std::thread::id handler_id{};
  std::atomic<bool> delivered{false};

  // Upstream with respect, unsubscribes
  auto src = observable<int>::create([&](auto on_next, auto, auto){
    auto alive = std::make_shared<std::atomic<bool>>(true);
    io.post([alive, on_next, &upstream_id]{
      upstream_id = std::this_thread::get_id();          // the stream where the upstream emits (io)
      if (!alive->load(std::memory_order_acquire)) return;
      if (on_next) on_next(123);
    });
    // After unsubscribing, we block further on_next
    return subscription([alive]{ alive->store(false, std::memory_order_release); });
  });

  auto sub = (src | observe_on(pool)).subscribe([&](int){
    handler_id = std::this_thread::get_id();             // pool thread
    delivered.store(true, std::memory_order_release);
  });

  // Wait for delivery with a timeout of ~200ms (without latch::try_wait_for)
  bool ok = false;
  for (int i = 0; i < 200; ++i) {
    if (delivered.load(std::memory_order_acquire)) { ok = true; break; }
    std::this_thread::sleep_for(1ms);
  }

  running.store(false);
  io_worker.join();
  sub.reset();

  assert(ok && "The message must be delivered within a reasonable time.");
  assert(handler_id != std::thread::id{} && upstream_id != std::thread::id{});
  assert(upstream_id != handler_id && "observe_on(pool) must transfer processing to a pool thread");

  std::cout << "[observe_on_threading_tests] OK\n";
  return 0;
}
