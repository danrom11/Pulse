# Pulse ⚡️

**Pulse** is a lightweight and fast reactive C++ library.  
It allows you to connect parts of your application with data streams (`observable`), transform them with operators, and easily propagate events between classes.

---

## ✨ Features

- `observable<T>` — data stream of any type, plus sources: `just`, `from`, `range`, `empty`, `never`, `error`, `defer`
- `topic<T>` — thread-safe event bus with per-subscriber executor, priority and backpressure
- `subject<T>` / `behavior_subject<T>` — hot sources (the latter replays the current value)
- **Object interaction (Qt-like, but simpler):**
  `signal<Args...>` + `connection`, `property<T>` with change notification and bindings, `computed<T>`
- Rich set of operators:
  `map`, `filter`, `tap`, `scan`, `reduce`, `skip`, `take`, `take_while`, `take_until`, `first`, `last`,
  `distinct`, `distinct_until_changed`, `pairwise`, `start_with`, `buffer`, `window`,
  `debounce`, `throttle`, `throttle_latest`, `delay`, `timeout`,
  `switch_map`, `concat_map`, `flat_map`, `merge`, `combine_latest`, `with_latest_from`, `zip`,
  `retry`, `catch_error`, `on_error_return`, `default_if_empty`, `finally_do`,
  `share`, `publish`, `ref_count`, `ref_count(grace)`, `observe_on`, `subscribe_on`
- Timers and intervals: `timer()`, `interval()`, `post_delayed()` — all driven by **one** shared
  `timer_service` thread (no thread per event)
- RAII subscriptions (`subscription`, `composite_subscription` / `subscription_bag`)
- Executors: `inline_executor`, `strand`, `thread_pool`

---

## 📦 Requirements

- **C++ standard:** C++20
- **CMake:** ≥ 3.21
- **Compilers (tested / recommended):**
  - GCC ≥ 12
  - Clang ≥ 14 / AppleClang ≥ 14
  - MSVC ≥ 19.36 (Visual Studio 2022 17.6)

> **Pulse** is header-only. No linking required, only include paths.

### Optional dependencies

- **Qt** — if `-DPULSE_WITH_QT=ON` (for `adapters/qt.hpp`)
- **CTest** — if `-DPULSE_BUILD_TESTS=ON`
- **Google Benchmark** — if `-DPULSE_BUILD_BENCHMARKS=ON`


## 🔧 Installation

```bash
git clone https://github.com/danrom11/Pulse.git
cd pulse
cmake -S . -B build
cmake --build build -j
cmake --install build --prefix /your/install/path
```

## ⚙️ Build Options

You can customize the build with the following CMake options:

| Option                     | Default          | Description                                          |
| -------------------------- | ---------------- | ---------------------------------------------------- |
| `PULSE_WITH_QT`            | `OFF`            | Enable Qt adapters (`adapters/qt.hpp`). Requires Qt. |
| `PULSE_BUILD_TESTS`        | top-level only   | Build unit tests.                                    |
| `PULSE_BUILD_EXAMPLES`     | top-level only   | Build example programs.                              |
| `PULSE_BUILD_BENCHMARKS`   | `OFF`            | Build benchmarks (requires Google Benchmark).        |
| `PULSE_RUN_TESTS_ON_BUILD` | `OFF`            | Run the test suite as part of the default target.    |
| `PULSE_INSTALL`            | top-level only   | Generate install / export rules.                     |

When Pulse is added with `add_subdirectory` / `FetchContent`, tests and examples are off by
default, so it does not slow down or interfere with the parent build. Run the tests with
`cmake --build build --target check`.

---

## 🧩 Using Pulse in your project

### Via `find_package` (after `install`)

```bash
cmake --install build --prefix /your/install/path
```

```cmake
find_package(Pulse REQUIRED)
target_link_libraries(your_target PRIVATE Pulse::pulse)
```

### Via `FetchContent` (without install)

```cmake
include(FetchContent)
FetchContent_Declare(
  pulse
  GIT_REPOSITORY https://github.com/danrom11/Pulse.git
  GIT_TAG main
)
FetchContent_MakeAvailable(pulse)

target_link_libraries(your_target PRIVATE Pulse::pulse)
```

---

## 🚀 Quick Start

### Minimal Example

```cpp
#include <pulse/pulse.hpp>
#include <iostream>
using namespace pulse;

int main() {
  inline_executor ui;

  topic<int> numbers;
  auto obs = as_observable(numbers, ui);

  auto sub = (obs | filter([](int x){ return x % 2 == 0; }))
    .subscribe([](int x){ std::cout << "even: " << x << "\n"; });

  numbers.publish(1);
  numbers.publish(2);
  numbers.publish(3);
  // => even: 2
}
```

---

### Connecting Classes

```cpp
class Producer {
public:
  void send(std::string msg) { bus_.publish(msg); }
  observable<std::string> stream(executor& ui) { return as_observable(bus_, ui); }
private:
  topic<std::string> bus_;
};

class Consumer {
public:
  Consumer(observable<std::string> input) {
    sub_ = input.subscribe([](const std::string& s){
      std::cout << "[Consumer] got: " << s << "\n";
    });
  }
private:
  subscription sub_;
};

int main() {
  inline_executor ui;
  Producer p;
  Consumer c(p.stream(ui));

  p.send("hello");
  p.send("world");
}
```

Output:
```
[Consumer] got: hello
[Consumer] got: world
```

---

### Reactive Search with Debounce

```cpp
SearchBox box;
SearchService service(box.stream(ui), ui, io);

box.type("que");
box.type("query"); 
// => [SearchService] [result] query
```

---

## 🔗 Object interaction: signals, properties, bindings

Pulse is first of all about **objects talking to each other**. For that you rarely need a full
stream pipeline — signals and properties are enough, and they plug into observables when you do.

```cpp
#include <pulse/pulse.hpp>

struct Player {
  pulse::property<int> score{0};      // value + change notification
  pulse::signal<std::string> died;    // event
};

struct Hud {
  pulse::property<std::string> label;
};

Player p;
Hud hud;

// one-way binding with a transform
hud.label.bind(p.score, [](int s) { return "Score: " + std::to_string(s); });

// classic connection (copyable handle, does not disconnect on destruction)
auto c = p.died.connect([](const std::string& why) { std::cout << "died: " << why << "\n"; });

// RAII connection
pulse::scoped_connection sc = p.score.changed.connect([](int s) { /* ... */ });

// derived value
pulse::computed<bool> high([&] { return p.score.get() > 100; }, p.score);

p.score = 150;          // hud.label == "Score: 150", high == true
p.died.emit("lava");

// everything is also a stream
auto sub = (p.score.as_observable() | pulse::filter([](int s) { return s % 10 == 0; }))
               .subscribe([](int s) { /* ... */ });
```

* `signal<Args...>` — thread-safe and re-entrant; a slot disconnected during an emit is not called
  afterwards; connections may outlive the signal. Queued delivery: `sig.connect(executor, fn)`.
* `property<T>` — emits `changed` only when the value really changes; `bind`, `bind_bidirectional`,
  `map`, `update`, `as_observable()` (current value + changes).
* `computed<T>` — read-only property recomputed when any of its sources change.

> Tip: with `using namespace pulse;` prefer writing `pulse::signal` — the C runtime also declares a
> global `::signal()` function.

---

## ⚡️ Hot vs Cold Observables

By default, an `observable` is “cold”: each subscriber restarts it.  
To share an upstream source:

```cpp
auto cold   = interval(100ms, io);
auto shared = ref_count(publish(cold), 250ms); // 250ms grace period

auto a = shared.subscribe([](auto v){ std::cout << "[A] " << v << "\n"; });
auto b = shared.subscribe([](auto v){ std::cout << "[B] " << v << "\n"; });
```

---

## 📚 Core Operators

* `map(f)` — transformation  
* `filter(f)` — filtering  
* `debounce(ms, exec)` — debounce (suppress intermediate events)  
* `distinct_until_changed()` — only propagate changes  
* `combine_latest(a, b, f)` — combine streams  
* `switch_map(f)` — switch to a new stream  
* `take(n)` — first N values  
* `zip(a,b)` — pairwise merge  
* `timeout(ms, exec)` — fail if no event within time  
* `throttle(ms, exec)` — emit first value per window  
* `throttle_latest(ms, exec)` — emit first + last value per window  
* `buffer(n)` — group N values into vectors  
* `window(n)` — sliding windows as nested observables  
* `merge(a,b)` — merge multiple streams  
* `concat_map(f)` — sequential map/flatten  
* `observe_on(exec)` — deliver on specified executor  
* `interval(period, exec, delay)` — periodic events  
* `timer(delay, exec)` — one-shot event  

---

## 🧹 Subscription Management

Every subscription returns a `subscription` object.  
When destroyed or reset, events stop flowing:

```cpp
auto sub = obs.subscribe(...);
sub.reset(); // unsubscribe
```

---

## 🏗 Architecture

* **observable<T>** — stream declaration (cold by default)
* **subscription / composite_subscription** — RAII cancellation
* **topic<T>, subject<T>, behavior_subject<T>** — hot sources; subscriptions hold only weak
  references, so destroying the source while subscriptions exist is safe
* **signal / property / computed** — object interaction layer
* **executor / strand / thread_pool** — execution context
* **timer_service** — one background thread for every time-based operator
* **publish / ref_count / share** — hot sharing

---

## 🧪 Testing

The project includes unit tests (`CTest` + `assert`; asserts stay enabled in Release builds of the tests).

```bash
cmake -S . -B build -DPULSE_BUILD_TESTS=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

Covers correctness of operators (`map`, `filter`, `window`, `buffer`, `merge`, `take`, `timeout`, …), unsubscription, and error propagation.

---

## ⚙️ Performance

* Minimal overhead — only lambda captures and a few `shared_ptr`.  
* No extra allocations in hot paths (operators are inline-friendly).  
* Multithreading supported via executors.  
* Comparable or faster than RxCpp in common cases.  

---

## 📐 Style Tips

* Use `auto` with operators to avoid verbose types.  
* Manage `subscription` lifetimes with RAII.  
* For async scenarios, use `thread_pool` or your own executor.  
* Use `merge` and `combine_latest` for multi-stream composition.  

---

## 🔄 Comparison

### Pulse vs RxCpp
|                | **Pulse**         | **RxCpp**         |
|----------------|-------------------|-------------------|
| API complexity | Minimal, focused  | Full Rx standard  |
| Language req.  | C++20             | C++11             |
| Executors      | Built-in          | None (external)   |
| Performance    | Low overhead      | Sometimes heavy   |
| Code size      | ~10 files         | >100 files        |

### Pulse vs std::execution
|                | **Pulse**                               | **std::execution** (C++20/23) |
|----------------|-----------------------------------------|-------------------------------|
| Paradigm       | Reactive streams (push model)           | Bulk execution, parallel loops (pull model) |
| Data           | Events over time (`observable<T>`)      | Containers / ranges            |
| Operators      | Reactive ops (`map`, `filter`, `zip`, …)| Parallel policies (`par`, `par_unseq`) |
| Asynchrony     | Built-in executors (`thread_pool`, …)   | Delegated to the implementation |
| Goal           | Event composition + reactive pipelines  | Efficient parallel algorithms  |

---

## 🗂 Version

The library defines [`pulse/version.hpp`](include/pulse/version.hpp):

```cpp
#include <pulse/version.hpp>
#include <iostream>

int main() {
  std::cout << "Pulse version: " << pulse::version::string << "\n";
}
```

You can check version macros:

```cpp
#if PULSE_VERSION_CODE >= 0x00020000
// code for version >= 2.0.0
#endif
```

---

## 📈 Benchmarks

We measured operator performance using [Google Benchmark](https://github.com/google/benchmark).  
Tests run in `Release` on macOS (AppleClang 15, 8 cores).

| Operator              | Size   | Time (ns)   | Per element (ns) | Events/sec (approx) | Notes |
|-----------------------|--------|-------------|------------------|----------------------|-------|
| `filter`              | 1000   | ~102 000    | ~100             | ~10 M/sec            | Simple filter pass |
| `map_chain (3×)`      | 1000   | ~170 000    | ~170             | ~6 M/sec             | Sequential 3 maps |
| `throttle_latest`     | 1000   | ~222 000    | ~222             | ~4.5 M/sec           | Timer + latest logic |
| `thread_pool(filter)` | 1000   | ~216 000    | ~216             | ~4.6 M/sec           | Cross-thread overhead |

✨ **Takeaways**:
- All operators scale **linearly (O(n))** with input size.  
- `inline_executor` achieves ~100 ns per element.  
- `thread_pool` and `throttle_latest` are ~2× heavier but provide async and backpressure control.  
- Even with heavy operators, Pulse processes **millions of events per second**.

---

## 🤝 Contributing

Pull requests are welcome!  
    1. Fork the repo  
    2. Create a branch (`git checkout -b feature/awesome-thing`)  
    3. Commit changes  
    4. Run tests (`ctest`)  
    5. Open PR 🎉  

---

## 📝 Changelog

### 0.2.0

**New**
- `signal<Args...>`, `connection`, `scoped_connection`, `property<T>`, `computed<T>`
- `behavior_subject<T>`, sources `just / from / range / empty / never / error / defer`
- Operators: `tap`, `scan`, `reduce`, `pairwise`, `skip`, `skip_while`, `take_while`, `take_until`,
  `first`, `last`, `distinct`, `delay`, `with_latest_from`, `flat_map`, `catch_error`,
  `on_error_return`, `default_if_empty`, `finally_do`, 3/4-ary `combine_latest`, pipeable `share()`
- `timer_service` + `post_delayed`; `timeout_error` type

**Fixed**
- `combine_latest` / `zip` captured the combiner by reference into the observable (dangling when the
  observable was a temporary); `zip` could complete twice and called `on_done` under its mutex
- `window` kept references to locals of the subscribe call (undefined behaviour for async sources)
- `distinct_until_changed` and `debounce` shared state between all subscribers
- `throttle` / `throttle_latest` slept *inside* the executor (froze UI threads) and ignored unsubscribe
- `timeout` passed only the first value and kept firing after unsubscribe
- `debounce` delivered values after unsubscribe and dropped the pending value on completion
- `retry` leaked through a self-referencing `shared_ptr`, and could cancel a live attempt
- `switch_map` completed on inner completion and did not cancel the inner stream on unsubscribe
- `topic` was not thread-safe, ignored the passed backpressure policy instance (e.g. `bp_buffer{2}`),
  crashed on re-entrant publish and when subscriptions outlived it; queued deliveries now skip
  unsubscribed handlers
- `subject` subscriptions dangled when the subject was destroyed
- `subscription::cancel_on_destruct(false)` was ignored
- `ref_count` lost values of synchronous sources; `share` grew without bound and replaced the original
  error with a generic one; `bp_batch_count_or_timeout_nms` used a moved-from handler
- Timers no longer spawn a thread per event
- CMake: duplicated install rules, undefined `PULSE_TRACE`, tests running on every build when used as
  a subproject, `CmakeLists.txt` casing

---

## 🛣 Roadmap

- [ ]   Support for custom executors (asio, libuv)
- [ ]   Support for the stdexec adapter
- [ ]   Tracing hooks
- [ ]   Additional operators (`group_by`, `replay`, ...)
- [ ]   Doxygen-style documentation
- [x]   UI integration: see [PulseUI](https://github.com/danrom11/PulseUI)

---

## 📜 License

Apache-2.0 License. Free to use and embed in your projects 🚀
