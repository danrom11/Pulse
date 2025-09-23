#include <pulse/pulse.hpp>
#include <chrono>
#include <thread>
#include <latch>
#include <atomic>
#include <cassert>
#include <iostream>

using namespace pulse;
using namespace std::chrono_literals;

// Simulates a "search": returns the result after ~120ms on the io executor
static observable<std::string> fake_search(std::string s, executor& io) {
  return observable<std::string>::create([s = std::move(s), &io](auto on_next, auto, auto){
    io.post([s, on_next]{
      std::this_thread::sleep_for(120ms);
      if (on_next) on_next("result for: " + s);
    });
    return subscription{};
  });
}

// Utility: a source that emits a value once and completes the subscription
template <class T>
static observable<T> single_value(T v) {
  return observable<T>::create([v = std::move(v)](auto on_next, auto, auto){
    if (on_next) on_next(v);
    return subscription{};
  });
}

int main() {
  inline_executor ui;
  strand io;

  // === CASE 1: SUCCESS (timeout 200ms > 120ms work) ===
  {
    std::latch done{1};
    std::string got;
    auto sub = (single_value(1)
      | switch_map([&](int){ return fake_search("ok", io); })
      | timeout(200ms)
      | observe_on(ui)
    ).subscribe(
      [&](const std::string& r){ got = r; done.count_down(); },
      [&](std::exception_ptr){ assert(false && "There should be no timeout"); }
    );

    std::atomic<bool> running{true};
    std::thread t([&]{ while(running.load()) { io.drain(); std::this_thread::sleep_for(1ms);} io.drain(); });
    done.wait();
    running.store(false);
    t.join();
    sub.reset();
    assert(got == "result for: ok" && "The result should come without a timeout.");
  }

  // === CASE 2: ERROR (timeout 80ms < 120ms of work) ===
  {
    std::latch done{1};
    bool timed_out = false;
    auto sub = (single_value(1)
      | switch_map([&](int){ return fake_search("slow", io); })
      | timeout(80ms)
      | observe_on(ui)
    ).subscribe(
      [&](const std::string&){ /* no-op */ },
      [&](std::exception_ptr){ timed_out = true; done.count_down(); }
    );

    std::atomic<bool> running{true};
    std::thread t([&]{ while(running.load()) { io.drain(); std::this_thread::sleep_for(1ms);} io.drain(); });
    done.wait();
    running.store(false);
    t.join();
    sub.reset();
    assert(timed_out && "A timeout must occur.");
  }

  std::cout << "[timeout_success_failure_tests] OK\n";
  return 0;
}
