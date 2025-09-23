#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <latch>
#include <atomic>

using namespace pulse;

struct Query { std::string text; };

int main() {
  inline_executor ui;
  strand io;

  topic<Query> queries;

  // q -> text -> distinct -> len>=2 -> debounce(50ms)
  auto qstream = as_observable(queries, ui)
    | map([](const Query& q){ return q.text; })
    | distinct_until_changed()
    | filter([](const std::string& s){ return s.size() >= 2; })
    | debounce(std::chrono::milliseconds(50), ui);

  // "search" with a delay of 120ms (longer than timeout)
  auto fake_search = [&](std::string s){
    return observable<std::string>::create([s = std::move(s), &io](auto on_next, auto, auto){
      io.post([s, on_next]{
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        on_next("result for: " + s);
      });
      return subscription{};
    });
  };

  // We only take the last one, but if there's no response within 80ms, use on_error(timeout)
  // Set it to 200 to see ab
  auto results = qstream
    | switch_map(fake_search)
    | timeout(std::chrono::milliseconds(80))
    | observe_on(ui);

  std::latch done{1};

  auto sub = results.subscribe(
    [&](const std::string& r){
      std::cout << "[OK] " << r << "\n";
      done.count_down();
    },
    [&](std::exception_ptr e){
      try { std::rethrow_exception(e); }
      catch(const std::exception& ex){
        std::cout << "[TIMEOUT] " << ex.what() << "\n";
        done.count_down();
      }
    }
  );

  // "input": duplicate "a" will collapse, "ab" will pass
  queries.publish(Query{"a"});
  queries.publish(Query{"a"});   // duplicate
  queries.publish(Query{"ab"});  // will go further

  // Background: spin io.drain() while waiting for the result
  std::atomic<bool> running{true};
  std::thread io_worker([&]{
    using namespace std::chrono_literals;
    while (running.load(std::memory_order_relaxed)) {
      io.drain();
      std::this_thread::sleep_for(1ms);
    }
    io.drain(); // final drain
  });

  // Wait for one event (OK or TIMEOUT), then stop IO
  done.wait();
  running.store(false, std::memory_order_relaxed);
  io_worker.join();

  return 0;
}
