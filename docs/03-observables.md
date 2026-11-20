# 3. Observables

## What an observable is

`pulse::observable<T>` represents **a sequence of values of type `T` that arrive over time**, followed
optionally by exactly one terminal event: *completion* or *error*.

```
--a--b--c--|      three values, then completion
--a--b--X         two values, then an error
--a--b--c-->      values forever (never terminates)
-----|            completes without values
```

The contract every observable follows:

1. `on_next` may be called zero or more times.
2. Then **at most one** of `on_error` or `on_completed` is called.
3. After a terminal event, nothing else is delivered.
4. Callbacks of one subscription are never called concurrently with each other, unless you use an
   operator that explicitly merges concurrent sources (`merge`, `flat_map`) from several threads.

## Subscribing

```cpp
pulse::subscription s = source.subscribe(
    [](const T& value) { /* on_next */ },
    [](std::exception_ptr e) { /* on_error (optional) */ },
    [] { /* on_completed (optional) */ });
```

- Only the first callback is required. Pass `{}` to skip one: `subscribe(on_next, {}, on_done)`.
- `on_error` receives a `std::exception_ptr`. Rethrow it to inspect:

  ```cpp
  [](std::exception_ptr e) {
    try { std::rethrow_exception(e); }
    catch (const pulse::timeout_error&) { /* ... */ }
    catch (const std::exception& ex) { std::cerr << ex.what(); }
  }
  ```
- The returned `subscription` **owns** the running stream. Keep it as long as you want values; destroy
  or `reset()` it to stop. See [Subscriptions and lifetime](04-subscriptions.md).

> **Common mistake:** `source.subscribe(f);` with the result discarded subscribes and then
> *immediately* unsubscribes, because the temporary subscription is destroyed at the end of the
> statement. Synchronous sources (such as `just`) still deliver their values, which hides the bug until
> the source becomes asynchronous.

## Creating observables

### Built-in sources

| Source | Emits | Notes |
|---|---|---|
| `just(a, b, c)` | `a`, `b`, `c`, then completes | Synchronously, on subscribe |
| `from(container)` | every element, then completes | Copies the container once; works with any range |
| `from({1, 2, 3})` | initializer-list form | |
| `range(start, count)` | `start`, `start+1`, ... (`count` values) | `range<long>(0, 10)` for other types |
| `empty<T>()` | completes immediately | Useful as "nothing" in `switch_map` |
| `never<T>()` | nothing, never terminates | Useful in tests and as a placeholder |
| `error<T>(exception)` | errors immediately | `error<int>(std::runtime_error("x"))` or an `exception_ptr` |
| `defer(factory)` | whatever `factory()` returns | Calls `factory` on **every** subscription |
| `timer(due, ex)` | `0` after `due`, then completes | Delivered on executor `ex` |
| `interval(period, ex, initial_delay = 0ms)` | `0, 1, 2, ...` every `period` | `std::size_t`; until unsubscribed |

Hot sources, which you push values into, have [their own chapter](06-hot-sources.md):
`subject<T>`, `behavior_subject<T>`, `topic<T>`, plus the observable views of `signal` and
`property`.

```cpp
auto words = pulse::from(std::vector<std::string>{"a", "bb", "ccc"});
auto lengths = words | pulse::map([](const std::string& s) { return s.size(); });
auto s = lengths.subscribe([](std::size_t n) { std::cout << n << " "; });   // 1 2 3
```

### `defer`: fresh state per subscriber

```cpp
auto now = pulse::defer([] { return pulse::just(std::chrono::system_clock::now()); });
// each subscribe gets the time of *its* subscription, not the time `now` was built
```

### `observable<T>::create`: your own source

`create` takes a function that is called once per subscription with the three callbacks and returns a
`subscription` that cancels whatever it started.

```cpp
// A stream of lines read by a background thread.
pulse::observable<std::string> read_lines(std::string path) {
  return pulse::observable<std::string>::create([path](auto on_next, auto on_error, auto on_done) {
    auto stop = std::make_shared<std::atomic<bool>>(false);
    std::thread([=] {
      std::ifstream in(path);
      if (!in) { on_error(std::make_exception_ptr(std::runtime_error("cannot open " + path))); return; }
      std::string line;
      while (!*stop && std::getline(in, line)) on_next(line);
      if (!*stop) on_done();
    }).detach();
    return pulse::subscription([stop] { *stop = true; });   // cancellation
  });
}
```

Rules for writing a source:

- Check the callbacks you call for emptiness (`if (on_done) on_done();`) if you might be given
  `{}`. The built-in operators always pass all three, so this matters only for direct subscribers.
- Respect cancellation: after the returned subscription is cancelled, stop calling the callbacks as
  soon as practical.
- Do not call anything after a terminal event.
- Return `pulse::subscription{}` if there is nothing to cancel (synchronous sources).

## Pipelines: the `|` operator

Operators are function objects. `source | op` is exactly `op(source)` and returns a new observable:

```cpp
auto evens_squared = pulse::range(1, 10)
                   | pulse::filter([](int x) { return x % 2 == 0; })
                   | pulse::map([](int x) { return x * x; });
```

Nothing runs until `subscribe`. Each operator subscribes to its upstream when *it* is subscribed to,
so subscribing to `evens_squared` subscribes to `filter`, which subscribes to `range`.

A pipeline is a value. You can store it, return it from functions, and subscribe to it many times:

```cpp
class Thermometer {
public:
  pulse::observable<double> celsius() const { return readings_.as_observable(); }
  pulse::observable<double> fahrenheit() const {
    return celsius() | pulse::map([](double c) { return c * 9 / 5 + 32; });
  }
private:
  pulse::subject<double> readings_;
};
```

### Writing your own operator

An operator is any callable that takes an `observable<T>` and returns an observable. A lambda works:

```cpp
auto clamp_to = [](int lo, int hi) {
  return [lo, hi](const pulse::observable<int>& src) {
    return src | pulse::map([lo, hi](int v) { return std::clamp(v, lo, hi); });
  };
};

auto s = (pulse::just(-5, 3, 99) | clamp_to(0, 10)).subscribe(print);   // 0 3 10
```

For operators with per-subscription state, use `observable<T>::create` inside, as the built-in
operators do (see `include/pulse/ops/skip.hpp` for small, readable examples).

## Cold and hot

This distinction explains most "why did that happen twice?" questions.

**Cold** observables start their work *per subscriber*. `just`, `from`, `range`, `timer`, `interval`,
`defer`, `create`-based sources and anything built from them with operators are cold:

```cpp
auto ticks = pulse::interval(1s, pool);
auto a = ticks.subscribe(print);   // starts timer #1: 0, 1, 2...
auto b = ticks.subscribe(print);   // starts timer #2, independent: 0, 1, 2...
```

**Hot** observables exist independently of subscribers. They emit whether or not anyone listens,
and a subscriber sees only what happens after it subscribed. `subject`, `topic`, `signal::as_observable`,
and `property::as_observable` (which also emits the current value first) are hot:

```cpp
pulse::subject<int> clicks;
clicks.on_next(1);                      // nobody listens: lost
auto s = clicks.subscribe(print);
clicks.on_next(2);                      // prints 2
```

Turning cold into hot, to share one expensive upstream among many subscribers, is what
`share()`, `publish()` and `ref_count()` do; see [Sharing one source](11-sharing.md).

## Default-constructed observables

`pulse::observable<T>{}` has no source: `subscribe` returns an empty subscription and nothing is ever
emitted. `valid()` tells you whether an observable has a source. This is handy as an "optional
stream" member that is filled in later.

## `pulse::unit`

Streams that carry no data (a click, a timer tick, a `signal<>`) use `pulse::unit`, an empty type
whose values all compare equal:

```cpp
pulse::signal<> clicked;
auto s = clicked.as_observable().subscribe([](pulse::unit) { std::cout << "click\n"; });
```

Next: [Subscriptions and lifetime](04-subscriptions.md).
