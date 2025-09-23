#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <latch>
#include <atomic>

using namespace pulse;
using namespace std::chrono;

int main() {
  inline_executor ui;
  thread_pool io{2};

  // Cold source: on connect(), run interval(100ms) in the pool.
  auto cold = observable<std::string>::create([&](auto on_next, auto, auto /*on_done*/){
    static std::atomic<int> session_counter{0};
    const int session_id = ++session_counter;

    // interval source: start after 80ms, then every 100ms (shift the phase a little)
    auto ticks = interval(100ms, io, 80ms);

    subscription sub = ticks.subscribe(
      [=](std::size_t k){
        if (on_next) on_next("session#" + std::to_string(session_id) +
                             " tick#" + std::to_string(k));
      }
    );

    return subscription([s = std::make_shared<subscription>(std::move(sub))]{
      s->reset();
    });
  });

  // grace = 250ms: keep the connection if subscribers "blink" faster
  auto shared = ref_count(publish(cold), 250ms);

  // === Phase A/B ===
  std::latch ab{2};
  auto a = (shared | take(1)).subscribe([&](const std::string& v){
    std::cout << "[A] " << v << "\n"; ab.count_down();
  });
  auto b = (shared | take(1)).subscribe([&](const std::string& v){
    std::cout << "[B] " << v << "\n"; ab.count_down();
  });

  // watchdog just in case (will not let the phase hang)
  std::latch watchdog_ab{1};
  auto w_ab = timer(600ms, ui).subscribe([&](int){
    if (watchdog_ab.try_wait() == false) {
      std::cout << "[TIMEOUT] AB phase took >600ms\n";
      // ничего не делаем — просто репорт
    }
  });

  ab.wait();
  watchdog_ab.count_down(); // deactivate watchdog

  // === Phase C (within grace) ===
  std::latch c1{1};
  subscription sc;
  // sign C after 180ms (exactly < 250ms grace and does not coincide with the 100ms)
  auto tC = timer(180ms, ui).subscribe([&](int){
    sc = (shared | take(1)).subscribe([&](const std::string& v){
      std::cout << "[C] " << v << "\n"; c1.count_down();
    });
  });

  std::latch watchdog_c{1};
  auto w_c = timer(800ms, ui).subscribe([&](int){
    if (watchdog_c.try_wait() == false) {
      std::cout << "[TIMEOUT] C phase took >800ms\n";
      c1.count_down(); // don't wait forever
    }
  });

  c1.wait();
  watchdog_c.count_down();

  // === Phase D (after grace) ===
  std::latch d1{1};
  subscription sd;
  // we'll sign D 650ms after the program starts - guaranteed after grace
  auto tD = timer(650ms, ui).subscribe([&](int){
    sd = (shared | take(1)).subscribe([&](const std::string& v){
      std::cout << "[D] " << v << "\n"; d1.count_down();
    });
  });

  std::latch watchdog_d{1};
  auto w_d = timer(1200ms, ui).subscribe([&](int){
    if (watchdog_d.try_wait() == false) {
      std::cout << "[TIMEOUT] D phase took >1200ms\n";
      d1.count_down();
    }
  });

  d1.wait();
  watchdog_d.count_down();

  return 0;
}
