#include <cassert>
#include <iostream>
#include <vector>
#include <stdexcept>

#include <pulse/pulse.hpp>

using namespace pulse;

// synchronous range source
static observable<int> range(int a, int b) {
  return observable<int>::create([=](auto on_next, auto, auto on_done){
    for (int x=a; x<=b; ++x) if (on_next) on_next(x);
    if (on_done) on_done();
    return subscription{};
  });
}

int main() {
  // 1) Base order: strict concatenation
  {
    auto src = range(1,3);
    auto cm = concat_map([](int x){
      return observable<int>::create([=](auto on_next, auto, auto on_done){
        if (on_next) on_next(x*10 + 1);
        if (on_next) on_next(x*10 + 2);
        if (on_done) on_done();
        return subscription{};
      });
    });

    std::vector<int> got;
    auto sub = (src | cm).subscribe([&](int v){ got.push_back(v); });
    std::vector<int> exp = {11,12,21,22,31,32};
    assert(got == exp && "concat_map must be sequential");
  }

  // 2) Error in the internal - everything is interrupted
  {
    auto src = range(1,3);
    auto cm = concat_map([](int x){
      return observable<int>::create([=](auto on_next, auto on_err, auto on_done){
        if (x == 2) {
          if (on_next) on_next(21);
          if (on_err) on_err(std::make_exception_ptr(std::runtime_error("boom")));
          return subscription{};
        }
        if (on_next) on_next(x*10);
        if (on_done) on_done();
        return subscription{};
      });
    });

    std::vector<int> got;
    bool got_err = false;

    auto sub = (src | cm).subscribe(
      [&](int v){ got.push_back(v); },
      [&](std::exception_ptr){ got_err = true; }
    );

    assert(got_err);
    std::vector<int> exp = {10, 21};
    assert(got == exp);
  }

  // 3) Reentrant unsubscribe from on_next - shouldn't crash
  {
    subject<int> s;

    auto cm = concat_map([](int x){
      return observable<int>::create([=](auto on_next, auto, auto on_done){
        if (on_next) on_next(x*100 + 1);
        if (on_next) on_next(x*100 + 2);
        if (on_done) on_done();
        return subscription{};
      });
    });

    std::vector<int> got;
    subscription sub;
    int seen = 0;

    sub = (s.as_observable() | cm).subscribe(
      [&](int v){
        got.push_back(v);
        if (++seen == 2) {
          // unsubscribe reentrantly right here - the operator is obliged to endure
          sub.reset();
        }
      }
    );

    s.on_next(1);   // will give 101, 102 and inside reset()
    s.on_next(2);   // ignore
    s.on_next(3);   // ignore
    s.on_completed();

    std::vector<int> exp = {101, 102};
    assert(got == exp && "unsubscribe should stop further emissions");
  }

  std::cout << "[concat_map_tests] OK\n";
  return 0;
}
