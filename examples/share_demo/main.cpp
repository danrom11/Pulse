#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <latch>

using namespace pulse;

int main() {
  inline_executor ui;
  thread_pool io{2};

  // "cold" source: does the work again with each subscription
  auto cold = observable<std::string>::create([&](auto on_next, auto, auto on_done){
    static std::atomic<int> runs{0};
    int id = ++runs;
    io.post([on_next, on_done, id]{
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      on_next("payload from run#" + std::to_string(id));
      if (on_done) on_done(); // <— end upstream (otherwise share stays "open")
    });
    return subscription{};
  });


  // Turn it into a "hot" and shared with all subscribers
  auto hot = share(cold);

  std::latch done{2};

  auto s1 = hot.subscribe([&](const std::string& v){
    std::cout << "[A] " << v << "\n";
    done.count_down();
  });
  auto s2 = hot.subscribe([&](const std::string& v){
    std::cout << "[B] " << v << "\n";
    done.count_down();
  });

  done.wait();
  return 0;
}
