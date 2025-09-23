#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <latch>

using namespace pulse;

struct Query { std::string text; };

int main() {
  inline_executor ui;     // "UI" - executes immediately
  thread_pool io{2};      // background for tasks (2 workers)

  topic<Query> queries;

  // Stream: text -> filter -> debounce(80ms)
  auto qstream = as_observable(queries, ui)
    | map([](const Query& q){ return q.text; })
    | filter([](const std::string& s){ return s.size() >= 2; })
    | debounce(std::chrono::milliseconds(80), ui);

  // Asynchronous search: runs on a pool, returns observable<string>
  auto fake_search = [&](std::string s){
    return observable<std::string>::create([s = std::move(s), &io](auto on_next, auto, auto){
      io.post([s, on_next]{
        std::this_thread::sleep_for(std::chrono::milliseconds(120)); // I/O simulation
        on_next("result for: " + s);
      });
      return subscription{};
    });
  };

  // Cancel previous searches and observe from the UI
  auto results = qstream
    | switch_map(fake_search)
    | observe_on(ui);

  std::latch done{1};
  auto sub = results.subscribe([&](const std::string& r){
    std::cout << "[POOL] " << r << "\n";
    done.count_down();
  });

  // "Noisy" user input
  queries.publish(Query{"q"});
  queries.publish(Query{"qu"});
  queries.publish(Query{"que"});
  queries.publish(Query{"quer"});
  queries.publish(Query{"query"});

  // Wait for the final result. No drain—the pool will do it all.
  done.wait();
  return 0;
}
