# Pulse ⚡️

**Pulse** is a lightweight and fast reactive C++ library.  
It allows you to connect parts of your application with data streams (`observable`), transform them with operators, and easily propagate events between classes.

---

## ✨ Features

- `observable<T>` — data stream of any type  
- `topic<T>` — event bus for publishing values  
- Rich set of operators:  
  `map`, `filter`, `debounce`, `distinct_until_changed`,  
  `combine_latest`, `switch_map`, `take`, `zip`,  
  `publish`, `ref_count`, `timeout`,  
  `throttle`, `throttle_latest`, `buffer`, `window`,  
  `merge`, `concat_map`, `observe_on` and more  
- Timers and intervals: `timer()`, `interval()`  
- Subscription management (`subscription`)  
- Hot and cold observables (`publish`, `ref_count`, `ref_count(grace)`)  
- Simple executors (`inline_executor`, `thread_pool`)  

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

| Option                   | Default | Description                                               |
| ------------------------ | ------- | --------------------------------------------------------- |
| `PULSE_WITH_QT`          | `OFF`   | Enable Qt adapters (`adapters/qt.hpp`). Requires Qt.      |
| `PULSE_BUILD_TESTS`      | `ON`    | Build unit tests.                                         |
| `PULSE_BUILD_EXAMPLES`   | `ON`    | Build example programs.                                   |
| `PULSE_BUILD_BENCHMARKS` | `OFF`   | Build benchmarks (requires Google Benchmark).             |
| `PULSE_TRACE`            | `OFF`   | Enable tracing hooks (experimental, not yet implemented). |

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
  GIT_REPOSITORY https://github.com/yourname/pulse.git
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

* **observable<T>** — stream declaration  
* **subscription** — subscription management  
* **topic<T>** — event bus  
* **executor / thread_pool** — execution context  
* **publish / ref_count** — hot sharing  

---

## 🧪 Testing

The project includes unit tests (`CTest` + `assert`).  

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

## 🛣 Roadmap

- [ ]   Support for custom executors (asio, libuv)
- [ ]   Support for the stdexec.hpp adapter
- [ ]   Tracing hooks
- [ ]   Additional operators (`group_by`, `replay`, and others)
- [ ]   Doxygen-style documentation
- [ ]   More examples of integration with the Qt UI framework  

---

## 📜 License

Apache-2.0 License. Free to use and embed in your projects 🚀
