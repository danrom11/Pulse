# 13. Patterns and pitfalls

## Patterns

### Model / view model / view

Keep state in **properties** on a model that knows nothing about the UI; derive display state with
**computed**; announce one-off events with **signals**. Views bind to them. This makes the model
testable without any UI.

```cpp
struct Player {
  pulse::property<double> position{0}, duration{0};
  pulse::property<bool> playing{false};
  pulse::signal<std::string> error;

  pulse::computed<double> progress;
  pulse::computed<std::string> time_label;

  Player()
      : progress([this] { return duration.get() > 0 ? position.get() / duration.get() : 0.0; }, position, duration),
        time_label([this] { return fmt_time(position.get()) + " / " + fmt_time(duration.get()); }, position, duration) {}
};
```

### Search as you type

```cpp
auto results = query.as_observable()
             | pulse::map(trim)
             | pulse::debounce(250ms, ui)            // wait for a pause
             | pulse::distinct_until_changed()       // ignore "abc" -> "abcd" -> "abc"
             | pulse::filter([](const std::string& q) { return q.size() >= 2; })
             | pulse::switch_map([&](const std::string& q) {   // cancel the stale request
                 return api.search(q) | pulse::on_error_return(Results{});
               });
```

### Loading details for the selection

```cpp
auto details = selected_id.as_observable()
             | pulse::switch_map([&](int id) -> pulse::observable<Details> {
                 if (id == 0) return pulse::just(Details{});        // nothing selected
                 return repo.load(id) | pulse::start_with(Details::loading());
               });
```

### Autosave after edits pause

```cpp
auto autosave = document.as_observable()
              | pulse::skip(1)                       // not the initial value
              | pulse::debounce(1s, ui)
              | pulse::concat_map([&](const Doc& d) { return storage.save_async(d); });   // never overlap saves
```

### Polling with backoff and a stop condition

```cpp
auto status = pulse::interval(2s, pool)
            | pulse::switch_map([&](std::size_t) { return api.job_status(id) | pulse::timeout(5s); })
            | pulse::retry(3)
            | pulse::take_while([](const Status& s) { return !s.finished; });
```

### Event bus between subsystems

```cpp
struct Bus {
  pulse::topic<UserLoggedIn> logged_in;
  pulse::topic<FileSaved> file_saved;
};

// subsystem A publishes
bus.file_saved.publish({path});

// subsystem B listens on its own thread; subsystem C on the UI thread, latest only
auto b = bus.file_saved.subscribe(indexer_pool, [](const FileSaved& e) { reindex(e.path); });
auto c = bus.file_saved.subscribe(ui, pulse::priority{0}, pulse::bp_latest<FileSaved>{}, show_toast);
```

### Bridging a worker thread to the UI

```cpp
class Exporter {
public:
  pulse::property<double> progress{0};
  pulse::signal<std::string> done;

  void start() {
    worker_ = std::jthread([this](std::stop_token st) {
      for (int i = 0; i <= 100 && !st.stop_requested(); ++i) {
        do_chunk(i);
        progress = i / 100.0;            // set from the worker thread: thread-safe
      }
      done("export.pdf");
    });
  }
private:
  std::jthread worker_;
};

// in the UI: deliver on the UI thread
exporter.progress.changed.connect(ui, [&](double p) { bar.set_value(p); });
exporter.done.connect(ui, [&](const std::string& f) { open(f); });
```

### Undo-friendly state

Because `property::update` produces a new value and emits it, you can record history with a
subscription:

```cpp
std::vector<Doc> history;
auto rec = doc.changed.connect([&](const Doc& d) { history.push_back(d); });
auto undo = [&] { if (history.size() > 1) { history.pop_back(); auto prev = history.back(); history.pop_back(); doc = prev; } };
```

## Pitfalls

### 1. Discarding the subscription

```cpp
source.subscribe(handler);          // subscribes, then unsubscribes at the semicolon
```

Store it in a member, a local that lives long enough, or a bag. For deliberate fire-and-forget on
finite streams, write `.release()`.

### 2. Capturing `this` in a subscription that outlives the object

```cpp
global_bus.subscribe([this](auto&) { ... });   // the bus outlives `this`
```

Store the returned subscription **in** the object. For signal connections, use `scoped()` or
`scoped_connection`.

### 3. Iterating over `property::get()` twice

`get()` returns a copy. `std::sort(p.get().begin(), p.get().end())` uses iterators into two different
temporaries, which is undefined behaviour (usually a crash). Take one copy: `auto v = p.get();`.
Do not return pointers or references into `get()`'s result.

### 4. Expecting `changed` to fire for the current value

`changed` fires only on future changes. Use `as_observable()` or `map()` when the listener needs the
current value as well.

### 5. Types without `operator==` in properties

`property<T>` compares values to suppress redundant notifications, so `T` needs `operator==`. Add
`bool operator==(const T&) const = default;` to your structs, or pass a custom `Eq`.

### 6. Forgetting a dependency of `computed`

```cpp
pulse::computed total([&] { return price.get() * qty.get() * tax.get(); }, price, qty);   // tax missing
```

`total` does not update when `tax` changes. List every property the function reads.

### 7. Executors that die too early

`debounce(300ms, some_local_strand)` with the strand destroyed while the subscription lives is a
use-after-free. Declare executors before, and destroy them after, the subscriptions that use them.

### 8. Blocking inside callbacks

Callbacks run on the producer's thread. Blocking in a property's `changed` slot blocks whoever called
`set`, possibly the UI thread. Blocking in a timer-driven stream delays every timer in the process,
because they share one thread, unless you used an executor. Move heavy work to a pool with
`observe_on(pool)` or `subscribe_on(pool)`.

### 9. Throwing from callbacks

Exceptions escape into the producer (`emit`, `set`, `publish`, `on_next`). They are not converted into
`on_error`. Catch inside, or model failures as values.

### 10. `flat_map` without bounds

`flat_map` starts an inner stream for every value, concurrently, without a limit. For user-driven
requests you almost always want `switch_map` (latest wins) or `concat_map` (one at a time).

### 11. Expecting order from a thread pool

`observe_on(pool)` with a multi-threaded pool can reorder values. Use a strand or a single-threaded
executor when order matters.

### 12. `using namespace pulse;` and `signal`

On some platforms the global `::signal` from `<csignal>` makes `signal<int>` ambiguous. Write
`pulse::signal`.

Next: [Cheat sheet](14-cheatsheet.md).
