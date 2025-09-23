#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <latch>
#include <atomic>

using namespace pulse;

int main() {
  inline_executor ui;
  thread_pool io{2};

  // "Cold" source: each run does the work anew and terminates the thread
  auto cold = observable<std::string>::create([&](auto on_next, auto, auto on_done){
    static std::atomic<int> runs{0};
    int id = ++runs;
    io.post([on_next, on_done, id]{
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      on_next(std::string("payload from run#") + std::to_string(id));
      if (on_done) on_done();
    });
    return subscription{};
  });

  // publish + ref_count
  auto conn   = publish(cold);
  auto shared = ref_count(conn); // the first subscriber is connect(), the last one is disconnect()

  // Phase 1: Two subscribers at the same time -> one upstream launch
  std::latch first{2};

  auto s1 = shared.subscribe([&](const std::string& v){
    std::cout << "[A] " << v << "\n";
    first.count_down();
  });
  auto s2 = shared.subscribe([&](const std::string& v){
    std::cout << "[B] " << v << "\n";
    first.count_down();
  });

  first.wait();     // waited for A and B
  s1.reset();       // both unsubscribe → upstream stopped by ref_count
  s2.reset();

  // Phase 2: A new subscriber launches upstream for the second time
  std::latch second{1};

  auto s3 = shared.subscribe([&](const std::string& v){
    std::cout << "[C] " << v << "\n";
    second.count_down();
  });

  second.wait();
  s3.reset();

  return 0;
}
