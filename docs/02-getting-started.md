# 2. Getting started

## Requirements

- A C++20 compiler: MSVC 19.36+ (Visual Studio 2022 17.6+), GCC 12+ (GCC 11 works for most code),
  Clang 14+ / AppleClang 14+.
- CMake 3.21+ if you use the provided CMake package (optional; Pulse is header-only).
- The system threads library (`Threads::Threads`), which CMake links for you. Time-based operators
  use one background thread.

## Adding Pulse to your project

### Option A: FetchContent (recommended)

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApp CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(pulse
  URL https://github.com/danrom11/Pulse/archive/0.2.1.tar.gz)   # or GIT_REPOSITORY + GIT_TAG 0.2.1
FetchContent_MakeAvailable(pulse)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Pulse::pulse)
```

When Pulse is included this way, its tests, examples and install rules are switched off
automatically, so they do not slow down your build.

### Option B: add_subdirectory

```cmake
add_subdirectory(third_party/Pulse)
target_link_libraries(my_app PRIVATE Pulse::pulse)
```

### Option C: install and find_package

```bash
cmake -S Pulse -B build
cmake --install build --prefix /opt/pulse
```

```cmake
find_package(Pulse REQUIRED)            # pass -DCMAKE_PREFIX_PATH=/opt/pulse
target_link_libraries(my_app PRIVATE Pulse::pulse)
```

### Option D: no CMake

Add `Pulse/include` to the include path, compile as C++20 and link the threads library
(`-pthread` on GCC/Clang).

## Build options

These matter only when you build Pulse itself (for example to run its tests).

| Option | Default | Meaning |
|---|---|---|
| `PULSE_BUILD_TESTS` | ON when top-level | Build the unit tests (`cmake --build build --target check` runs them) |
| `PULSE_BUILD_EXAMPLES` | ON when top-level | Build the example programs in `examples/` |
| `PULSE_BUILD_BENCHMARKS` | OFF | Benchmarks (needs Google Benchmark) |
| `PULSE_RUN_TESTS_ON_BUILD` | OFF | Run the tests as part of the default build |
| `PULSE_INSTALL` | ON when top-level | Generate install and export rules |
| `PULSE_WITH_QT` | OFF | Build the Qt adapter examples (`adapters/qt.hpp`) |

## Headers

`#include <pulse/pulse.hpp>` includes everything. You can include individual headers if you care
about compile times:

| Header | Contents |
|---|---|
| `pulse/core/observable.hpp` | `observable<T>`, `unit` |
| `pulse/core/pipeline.hpp` | the `|` operator |
| `pulse/core/subscription.hpp` | `subscription`, `make_subscription` |
| `pulse/core/composite_subscription.hpp` | `composite_subscription` / `subscription_bag` |
| `pulse/core/sources.hpp` | `just`, `from`, `range`, `empty`, `never`, `error`, `defer` |
| `pulse/core/signal.hpp` | `signal`, `connection`, `scoped_connection`, `emit_into` |
| `pulse/core/property.hpp` | `property`, `computed` |
| `pulse/core/subject.hpp` | `subject`, `behavior_subject` |
| `pulse/core/topic.hpp`, `topic_to_observable.hpp` | `topic`, `priority`, `as_observable(topic)` |
| `pulse/core/scheduler.hpp` | `executor`, `inline_executor`, `strand`, `timer_service`, `post_delayed` |
| `pulse/core/thread_pool.hpp` | `thread_pool` |
| `pulse/core/backpressure.hpp` | `bp_*` policies |
| `pulse/ops/*.hpp` | one header per operator family |
| `pulse/adapters/qt.hpp` | Qt integration (not included by `pulse.hpp`) |

Everything lives in `namespace pulse`.

> **Name clash on MinGW and glibc:** `<csignal>` declares a global function `::signal`. If you write
> `using namespace pulse;` and then `signal<int> s;`, the compiler may report an ambiguity. Write
> `pulse::signal<int>` (recommended in headers anyway).

## Your first program

```cpp
#include <pulse/pulse.hpp>
#include <iostream>

int main() {
  using namespace pulse;

  // 1) object interaction: a property and a listener
  property<int> temperature{20};
  auto c = temperature.changed.connect([](int t) { std::cout << "temperature: " << t << "\n"; });

  temperature = 21;   // prints "temperature: 21"
  temperature = 21;   // nothing: the value did not change

  // 2) a stream pipeline over the same property
  auto alerts = temperature.as_observable()
              | filter([](int t) { return t > 30; })
              | map([](int t) { return "too hot: " + std::to_string(t); });

  auto sub = alerts.subscribe([](const std::string& s) { std::cout << s << "\n"; });

  temperature = 35;   // prints "temperature: 35" and "too hot: 35"
  temperature = 25;   // prints "temperature: 25" only

  sub.reset();        // stop the pipeline
  temperature = 40;   // prints "temperature: 40" only
}
```

What to notice:

- `temperature.changed` is a `signal<int>`. `connect` returns a `connection` handle.
- `as_observable()` turns the property into a stream that emits the current value and then every
  change.
- `subscribe` returns a `subscription`. The pipeline runs while it is alive; `reset()` (or
  destruction) stops it.

## A taste of time

```cpp
#include <pulse/pulse.hpp>
#include <iostream>
#include <thread>

int main() {
  using namespace pulse;
  using namespace std::chrono_literals;

  thread_pool pool{2};

  auto sub = (interval(100ms, pool)                       // 0, 1, 2, ... every 100 ms
              | map([](std::size_t i) { return i * i; })
              | take(5))                                  // 0, 1, 4, 9, 16, then complete
                 .subscribe([](std::size_t v) { std::cout << v << "\n"; },
                            {},                            // no error handler
                            [] { std::cout << "done\n"; });

  std::this_thread::sleep_for(700ms);
}
```

`interval` needs an executor to deliver its ticks on. Here a `thread_pool` is used; in a GUI app you
would pass the UI executor (PulseUI provides `pulseui::App::ui()`).

Next: [Observables](03-observables.md).
