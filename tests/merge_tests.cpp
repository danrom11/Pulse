#include <cassert>
#include <iostream>
#include <vector>
#include <stdexcept>

#include <pulse/pulse.hpp>

using namespace pulse;

// Mini-source, controlled from the test: next()/complete()/error()
// respects unsubscribe (via the alive flag).
struct Manual {
  using T = int;

  std::function<void(T)> emit;
  std::function<void()>  done;
  std::function<void(std::exception_ptr)> fail;

  observable<T> obs;

  Manual()
  : obs(observable<T>::create([this](auto on_next, auto on_error, auto on_completed){
      auto alive = std::make_shared<std::atomic<bool>>(true);

      emit = [alive, on_next](T v){
        if (alive && *alive && on_next) on_next(v);
      };
      done = [alive, on_completed]{
        if (alive && *alive && on_completed) on_completed();
      };
      fail = [alive, on_error](std::exception_ptr e){
        if (alive && *alive && on_error) on_error(e);
      };

      return subscription([alive]{
        if (alive) *alive = false;
      });
    }))
  {}
};

int main() {
  // 1) Synchronous sources: order - as signed, without reposting.
  // A: 1, 3, complete
  // B: 2, 4, complete
  // Expected: 1, 3, 2, 4 (A is signed and emitted before B).
  {
    auto A = observable<int>::create([](auto on_next, auto, auto on_completed){
      if (on_next) on_next(1);
      if (on_next) on_next(3);
      if (on_completed) on_completed();
      return subscription{};
    });
    auto B = observable<int>::create([](auto on_next, auto, auto on_completed){
      if (on_next) on_next(2);
      if (on_next) on_next(4);
      if (on_completed) on_completed();
      return subscription{};
    });

    std::vector<int> got;
    bool done = false;

    auto sub = merge<int>(A, B).subscribe(
      [&](int v){ got.push_back(v); },
      nullptr,
      [&]{ done = true; }
    );

    assert((got == std::vector<int>({1,3,2,4})) && "merge: wrong interleave for sync sources");
    assert(done && "merge: must complete when both sources completed");
  }

  // 2) An error from one source - instantly down, the second one unsubscribes.
  {
    Manual S; // second source (controlled)
    auto Err = observable<int>::create([](auto on_next, auto on_error, auto){
      if (on_next) on_next(10);
      if (on_error) on_error(std::make_exception_ptr(std::runtime_error("boom")));
      return subscription{};
    });

    std::vector<int> got;
    bool got_err = false;

    auto sub = merge<int>(Err, S.obs).subscribe(
      [&](int v){ got.push_back(v); },
      [&](std::exception_ptr){ got_err = true; }
    );

    // After an error from the first source, merge should unsubscribe from the second one,
    // so these calls should not get through.
    S.emit(99);
    S.done();

    assert((got == std::vector<int>({10})) && "merge: values from the other source must be canceled after error");
    assert(got_err && "merge: error must be propagated");
  }

  // 3) Completion when both are completed
  {
    Manual A, B;

    std::vector<int> got;
    bool done = false;

    auto sub = merge<int>(A.obs, B.obs).subscribe(
      [&](int v){ got.push_back(v); },
      nullptr,
      [&]{ done = true; }
    );

    A.emit(1);
    A.done();        // only one has completed - there is no done yet
    assert(!done && "merge: must not complete until all sources done");

    B.emit(2);
    assert(!done && "merge: not complete yet");
    B.done();        // now both are finished
    assert(done && "merge: must complete when all sources done");
    assert((got == std::vector<int>({1,2})) && "merge: values should pass through");
  }

  // 4) Variadic merge(a,b,c)
  {
    auto A = observable<int>::create([](auto on_next, auto, auto on_completed){
      if (on_next) on_next(1);
      if (on_completed) on_completed();
      return subscription{};
    });
    auto B = observable<int>::create([](auto on_next, auto, auto on_completed){
      if (on_next) on_next(2);
      if (on_completed) on_completed();
      return subscription{};
    });
    auto C = observable<int>::create([](auto on_next, auto, auto on_completed){
      if (on_next) on_next(3);
      if (on_completed) on_completed();
      return subscription{};
    });

    std::vector<int> got;
    bool done = false;

    auto sub = merge<int>(A, B, C).subscribe(
      [&](int v){ got.push_back(v); },
      nullptr,
      [&]{ done = true; }
    );

    assert((got == std::vector<int>({1,2,3})) && "merge(variadic): wrong order for sync simple case");
    assert(done && "merge(variadic): must complete");
  }

  std::cout << "[merge_tests] OK\n";
  return 0;
}
