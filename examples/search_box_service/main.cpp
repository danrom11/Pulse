#include <pulse/pulse.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

using namespace pulse;
using namespace std::chrono_literals;

// --- Source class: emulates user input -----------------------------
class SearchBox {
public:
  void type(std::string s) { text_.publish(std::move(s)); }
  observable<std::string> stream(executor& ui) { return as_observable(text_, ui); }

private:
  topic<std::string> text_;
};

// --- Service: Reactive Pipeline ---------------------------------------------
class SearchService {
public:
  SearchService(observable<std::string> input, executor& ui, thread_pool& io)
  : pipeline_(make_pipeline(std::move(input), ui, io)),
    sub_(pipeline_.subscribe([](const std::string& r){
      std::cout << "[SearchService] " << r << "\n";
    }))
  {}

private:
  static observable<std::string>
  make_pipeline(observable<std::string> input, executor& ui, thread_pool& io)
  {
    auto async_search = [&](std::string query){
      return observable<std::string>::create(
        [q = std::move(query), &io](auto on_next, auto /*on_err*/, auto /*on_done*/){
          io.post([q, on_next]{
            std::this_thread::sleep_for(250ms); // network simulation
            if (on_next) on_next(std::string("[result] ") + q);
          });
          return subscription{};
        }
      );
    };

    return input
      | filter([](const std::string& s){ return s.size() >= 2; })
      | debounce(200ms, ui)
      | switch_map(async_search)
      | observe_on(ui);
  }

private:
  observable<std::string> pipeline_;
  subscription             sub_;
};

// --- Demo ---------------------------------------------------------------------
int main() {
  inline_executor ui;
  thread_pool io{2};

  SearchBox box;
  SearchService service(box.stream(ui), ui, io);

  // "noisy" input
  box.type("q");      std::this_thread::sleep_for(50ms);
  box.type("qu");     std::this_thread::sleep_for(50ms);
  box.type("que");    std::this_thread::sleep_for(50ms);
  box.type("quer");   std::this_thread::sleep_for(50ms);
  box.type("query");

  std::this_thread::sleep_for(600ms);

  box.type("react");  std::this_thread::sleep_for(80ms);
  box.type("reacti"); std::this_thread::sleep_for(80ms);
  box.type("reactive");

  std::this_thread::sleep_for(600ms);
  return 0;
}
