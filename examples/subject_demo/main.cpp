#include <pulse/pulse.hpp>
#include <iostream>
#include <string>

using namespace pulse;

int main() {
  inline_executor ui;

  subject<std::string> subj;
  auto obs = subj.as_observable()
    | map([](const std::string& s){ return "[evt] " + s; })
    | observe_on(ui);

  auto s1 = obs.subscribe([](const std::string& s){ std::cout << "A " << s << "\n"; });
  auto s2 = obs.subscribe([](const std::string& s){ std::cout << "B " << s << "\n"; });

  subj.on_next("hello");
  subj.on_next("world");

  // unsubscribe B and send another event
  s2.reset();
  subj.on_next("only A hears this");

  // terminate the thread
  subj.on_completed();

  // new subscribers will see on_completed immediately after completion
  auto s3 = obs.subscribe(
    [](const std::string& s){ std::cout << "C got: " << s << "\n"; },
    nullptr,
    []{ std::cout << "C completed immediately\n"; }
  );

  return 0;
}

