#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
#include <pulse/pulse.hpp>
#include <string>
#include <thread>

using namespace pulse;

struct Query {
  std::string text;
};

int main() {
  // Performers
  inline_executor ui; // "UI" - executes immediately
  strand io;          // "IO" - task queue, executed manually

  // Request topic
  topic<Query> queries;

  // Stream: text -> filter -> debounce(100ms)
  auto qstream = as_observable(queries, ui) |
                 map([](const Query &q) { return q.text; }) |
                 filter([](const std::string &s) { return s.size() >= 2; }) |
                 debounce(std::chrono::milliseconds(100), ui);

  // Fake asynchronous search:
  // — publishes work to the io queue,
  // — "waits for the network" for 120ms, then returns the result.
  auto fake_search = [&](std::string s) {
    return observable<std::string>::create(
        [s = std::move(s), &io](auto on_next, auto, auto) {
          io.post([s, on_next] {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            on_next("result for: " + s); // emit из IO
          });
          return subscription{};
        });
  };

  // Cancel previous requests, deliver results to the UI
  auto results =
      qstream |
      switch_map(fake_search) // each new request cancels the previous one
      | observe_on(ui);       // deliver the result to the UI

  std::latch done{1};

  auto sub = results.subscribe([&](const std::string &r) {
    std::cout << "[SEARCH/ASYNC] " << r << "\n";
    done.count_down();
  });

  // Background: spin io.drain() until we get the result
  std::atomic<bool> running{true};
  std::thread io_worker([&] {
    using namespace std::chrono_literals;
    while (running.load(std::memory_order_relaxed)) {
      io.drain(); // execute all accumulated IO tasks
      std::this_thread::sleep_for(1ms);
    }
    // final drainage at the outlet
    io.drain();
  });

  // "noisy" user input
  queries.publish(Query{"q"});
  queries.publish(Query{"qu"});
  queries.publish(Query{"que"});
  queries.publish(Query{"quer"});
  queries.publish(Query{"query"});

  // We are waiting for the final result
  done.wait();
  running = false;
  io_worker.join();

  return 0;
}
