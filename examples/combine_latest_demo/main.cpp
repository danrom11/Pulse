#include <pulse/pulse.hpp>
#include <iostream>
#include <string>

using namespace pulse;

struct Name  { std::string value; };
struct Email { std::string value; };

int main() {
  inline_executor ui;

  topic<Name>  names;
  topic<Email> emails;

  // Validity streams
  auto nameOk = as_observable(names, ui)
    | map([](const Name& n){ return !n.value.empty() && n.value.size() >= 3; })
    | start_with(false) 
    | distinct_until_changed();

  auto emailOk = as_observable(emails, ui)
    | map([](const Email& e){
        auto at = e.value.find('@');
        auto dot = e.value.rfind('.');
        return at != std::string::npos && dot != std::string::npos && at < dot && dot+1 < e.value.size();
      })
    | start_with(false) 
    | distinct_until_changed();

  // Combine the last values: the button is active if both are true
  auto canSubmit = combine_latest<bool, bool>(
      nameOk, emailOk,
      [](const bool& a, const bool& b){ return a && b; }
    );

  auto sub = canSubmit.subscribe([](bool ok){
    std::cout << "[FORM] submit_enabled = " << (ok ? "true" : "false") << "\n";
  });

  // Scenario: first name, then email; observe state switching
  names.publish(Name{"Al"});        // false (short name)
  emails.publish(Email{"a@b"});     // still false (no domain dot)
  names.publish(Name{"Alex"});      // now depends on email -> still false
  emails.publish(Email{"alex@site.com"}); // true && true -> true

  // changing email to invalid -> false
  emails.publish(Email{"alex@site"});     // no point -> false

  // edit email -> true again
  emails.publish(Email{"alex@site.io"});  // true

  return 0;
}

