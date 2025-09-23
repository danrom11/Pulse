#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <latch>

using namespace pulse;

struct Query { std::string text; };

int main() {
  // Executor: we will execute everything synchronously for simplicity of the example
  inline_executor ui;

  // Topic for queries
  topic<Query> queries;

  // Observable request stream: text -> length filter -> debounce
  auto qstream = as_observable(queries, ui)
    | map([](const Query& q){ return q.text; })
    | filter([](const std::string& s){ return s.size() >= 2; })
    | debounce(std::chrono::milliseconds(100), ui);

  // Dummy async "search": there could be a real IO operation here
  auto fake_search = [&](std::string s){
    return observable<std::string>::create([s = std::move(s)](auto on_next, auto, auto){
      on_next("result for: " + s);
      return subscription{};
    });
  };

  // We take only the last request (canceling the previous ones)
  auto results = qstream
    | switch_map(fake_search)
    | observe_on(ui);

  std::latch done{1};

  // Subscribe to the results
  auto sub = results.subscribe([&](const std::string& r){
    std::cout << "[SEARCH] " << r << "\n";
    done.count_down();
  });

  // "Noisy" user input
  queries.publish(Query{"q"});
  queries.publish(Query{"qu"});
  queries.publish(Query{"que"});
  queries.publish(Query{"quer"});
  queries.publish(Query{"query"});

  // Wait for the result from debounce+switch_map
  done.wait();

  return 0;
}
