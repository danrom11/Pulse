# 5. Signals, properties, computed

This chapter covers the object-interaction half of Pulse. These three types let objects talk to each
other without knowing each other's types, like Qt signals and properties, but as plain C++ members.

```cpp
class Downloader {
public:
  pulse::signal<std::string> finished;          // event with a payload
  pulse::signal<std::string> failed;
  pulse::property<double> progress{0.0};        // state that others can watch
  void start(std::string url);
};

Downloader d;
d.progress.changed.connect([](double p) { std::cout << int(p * 100) << "%\n"; });
d.finished.connect([](const std::string& file) { open(file); });
```

## `pulse::signal<Args...>`

A synchronous, thread-safe list of callbacks ("slots").

```cpp
pulse::signal<>                  clicked;      // no arguments
pulse::signal<int>               value_changed;
pulse::signal<std::string, int>  progress;     // several arguments
```

### Connecting

```cpp
connection connect(F&& slot);                   // slot(const Args&...) or slot()
connection connect(executor& ex, F&& slot);     // queued: slot runs via ex.post(...)
connection connect(Obj* obj, R (Obj::*method)(P...));   // member function
connection forward_to(signal& other);           // re-emit into another signal of the same type
```

```cpp
value_changed.connect([](int v) { std::cout << v; });
value_changed.connect([] { std::cout << "changed\n"; });   // a slot may ignore the arguments
value_changed.connect(&logger, &Logger::on_value);          // void Logger::on_value(int)
value_changed.connect(ui_executor, [](int v) { label.set(v); });   // delivered on the UI thread
```

A slot may take **all** arguments or **none**. If you need some, write a lambda that takes all of
them and ignores the rest.

### Emitting

```cpp
void emit(const Args&... args) const;
void operator()(const Args&... args) const;    // same as emit
```

```cpp
value_changed.emit(42);
value_changed(42);          // same thing
clicked();                  // signal<>
```

> `emit` is a function call, not a subscription. `button.clicked(handler)` does **not** connect
> `handler`; it tries to emit `handler` as an argument and fails to compile. Use
> `button.clicked.connect(handler)`.

Emission semantics:

- **Synchronous:** direct slots run inside `emit`, on the emitting thread, in connection order.
- **Queued slots** (connected with an executor) receive a copy of the arguments, posted to their
  executor. They run later, on that executor.
- **Snapshot:** a slot connected during an emit is not called by that emit. A slot disconnected
  during an emit is not called afterwards, even if it was in the snapshot.
- **Re-entrant:** slots may emit the same signal, connect, or disconnect without deadlock.
- **Thread-safe:** connect, disconnect and emit may race freely.
- **Exceptions** thrown by a slot propagate out of `emit` and skip the remaining slots. Do not throw
  from slots.

### Other members

| Member | Purpose |
|---|---|
| `disconnect_all()` | Disconnect every slot |
| `slot_count()` | Number of connected slots (for tests and diagnostics) |
| `set_blocked(bool)` / `blocked()` | Temporarily suppress emission, like `QObject::blockSignals` |
| `as_observable()` | The signal as a hot stream (see below) |
| move constructor / assignment | Transfers all connections; the moved-from signal is empty but usable |

Signals are not copyable: copying an object with signals would silently duplicate or lose
connections.

### Connections

`connect` returns a `pulse::connection`, a small copyable handle:

```cpp
pulse::connection c = sig.connect(slot);
c.connected();          // still connected?
c.disconnect();         // idempotent; safe from any thread, even after the signal died
pulse::subscription s = c.scoped();        // RAII: disconnects when s is destroyed
```

A `connection` does **not** disconnect when destroyed. That is deliberate: most connections are
meant to live as long as both objects. For RAII use `scoped()`, the implicit conversion to
`subscription`, or `pulse::scoped_connection`:

```cpp
class Widget {
  pulse::scoped_connection on_theme_;
public:
  Widget(Theme& t) : on_theme_(t.changed.connect([this] { repaint(); })) {}
};   // disconnects automatically
```

**When must I disconnect?** Whenever the slot captures an object that may die before the signal.
In the `Widget` above, the theme outlives the widget, so the connection must end with the widget.

### Signals as streams

`as_observable()` gives a hot stream of emissions:

| Signal | Stream type |
|---|---|
| `signal<>` | `observable<pulse::unit>` |
| `signal<T>` | `observable<T>` |
| `signal<A, B, ...>` | `observable<std::tuple<A, B, ...>>` |

```cpp
auto double_clicks = clicked.as_observable()
                   | pulse::scan(std::pair{0, Clock::time_point{}}, detect_double_click)
                   | pulse::filter(...);
```

The reverse direction is `emit_into(observable, signal)`. It subscribes and re-emits every value:

```cpp
pulse::subscription s = pulse::emit_into(results_stream, results_ready);
```

## `pulse::property<T, Eq = std::equal_to<T>>`

A value plus a `changed` signal: the state that other parts of the program display or depend on.

```cpp
pulse::property<std::string> name{"untitled"};
pulse::property<std::vector<Item>> items;
pulse::property<bool> busy{false};
```

### Reading and writing

```cpp
T get() const;               // returns a COPY (thread safety)
T operator()() const;        // same: name()
operator T() const;          // implicit: std::string s = name;

bool set(T v);               // true if the value changed (then `changed` was emitted)
property& operator=(T v);    // same as set
template <class F> bool update(F&& f);   // modify a copy in place, then set
```

```cpp
name = "report.txt";                          // emits changed("report.txt")
name = "report.txt";                          // no-op: equal value
items.update([](auto& v) { v.push_back(Item{}); });   // read-modify-write
busy.set(true);
```

**Why `get()` returns a copy.** The value is protected by a mutex so properties can be written from
worker threads. Returning a reference would let the caller read it while another thread writes it.
For large values this costs a copy, which is usually fine for UI state. Two consequences:

```cpp
// WRONG: two different temporaries, so the iterators belong to different vectors
std::count(items.get().begin(), items.get().end(), x);

// RIGHT: take one copy
const auto v = items.get();
std::count(v.begin(), v.end(), x);

// Also fine: a range-for keeps the temporary alive for the whole loop
for (const Item& i : items.get()) { ... }

// WRONG: returns a pointer into a temporary
const Item* find(int id) { for (auto& i : items.get()) if (i.id == id) return &i; return nullptr; }
```

**Why `set` compares first.** Emitting only on real changes prevents feedback loops (A updates B,
B updates A) and avoids redundant work downstream. The comparison is `Eq{}(old, new)`, which is
`operator==` by default. Your type needs `operator==`; `= default` is usually enough:

```cpp
struct Todo {
  std::string title;
  bool done = false;
  bool operator==(const Todo&) const = default;
};
pulse::property<std::vector<Todo>> todos;   // vector<Todo>::operator== uses Todo::operator==
```

For types without a meaningful equality, or when every write must notify, provide your own `Eq`:

```cpp
struct always_different { template <class T> bool operator()(const T&, const T&) const { return false; } };
pulse::property<Frame, always_different> frame;   // every set() emits
```

### Watching

```cpp
signal<T> changed;                         // emitted with the new value after each change
connection on_change(F&& f);               // shorthand for changed.connect(f)
observable<T> as_observable() const;       // current value first, then every change
auto map(F f) const;                       // as_observable() | map(f)
```

The difference between `changed` and `as_observable()` matters:

```cpp
pulse::property<int> p{7};
p.changed.connect(print);                  // prints nothing yet; prints future changes
auto s = p.as_observable().subscribe(print);   // prints 7 immediately, then future changes
```

Use `as_observable()` (or `map`) when the listener needs the current state, which is almost always
true for display. Use `changed` for reactions to transitions ("when it becomes true, start X").

`map` is a convenient derived stream:

```cpp
auto label = count.map([](int c) { return "Clicked " + std::to_string(c) + " times"; });
// observable<std::string>: current text now, new text on every change
```

### Bindings

```cpp
void bind(const observable<T>& src);       // follow a stream: set(v) for every value
void bind(const property& src);            // follow another property
void bind(const property<U>& src, F map);  // follow with conversion
void unbind();
bool is_bound() const;
subscription bind_bidirectional(property& other);
```

A one-way binding makes the property follow a source until `unbind()`, another `bind()`, or the
property's destruction:

```cpp
pulse::property<std::string> status;
status.bind(progress, [](double p) { return std::to_string(int(p * 100)) + "%"; });
```

A two-way binding keeps two properties equal. The initial value comes from `other`. The equality
check in `set` stops the ping-pong:

```cpp
pulse::property<double> slider_value, model_volume{0.3};
auto link = slider_value.bind_bidirectional(model_volume);   // both are 0.3 now
slider_value = 0.8;   // model_volume becomes 0.8, which emits back into slider_value: equal, stops
```

`bind_bidirectional` returns a subscription; the link lasts while you keep it.

### Thread safety

`get`, `set`, `update` and `changed` are safe from any thread. `changed` slots run on the thread that
called `set`. If a worker thread sets a property that the UI watches, deliver to the UI thread with a
queued connection or `observe_on`:

```cpp
progress.changed.connect(ui_executor, [](double p) { bar.set_value(p); });
auto s = (progress.as_observable() | pulse::observe_on(ui_executor)).subscribe(update_bar);
```

`update` is "copy, modify, set". It is **not** atomic against a concurrent `update` from another
thread; one of the two modifications can be lost. Serialize writers (for example through a
`strand` or the UI thread) if several threads modify the same property.

## `pulse::computed<R>`

A read-only property whose value is a function of other properties. It recomputes whenever one of
the listed sources changes.

```cpp
pulse::property<double> price{9.99};
pulse::property<int> quantity{3};

pulse::computed total([&] { return price.get() * quantity.get(); }, price, quantity);

total.get();                         // 29.97
quantity = 4;                        // total recomputes to 39.96 and emits changed
```

```cpp
template <class F, class... Sources>
explicit computed(F f, Sources&... sources);   // sources: anything with a `changed` signal

R get() const;   R operator()() const;   operator R() const;
signal<R>& changed;
observable<R> as_observable() const;
auto map(F f) const;
void recompute();                     // force a recomputation (when f reads something unlisted)
```

Details:

- The function runs once in the constructor, then on every `changed` of any source.
- **You list the dependencies explicitly.** There is no magic tracking. If `f` reads something
  you did not list, `computed` will not know it changed. List it, or call `recompute()`.
- Sources can be `property`, other `computed`, or anything with a `changed` signal. Chains work:
  `computed` of `computed` of `property`.
- Like `property`, it emits only when the result actually changes (`R` needs `operator==`).
- The sources must outlive the `computed`. As class members, declare the sources first.
- The template argument is deduced: `pulse::computed total(...)` gives `computed<double>` above.
  As a class member you must spell the type: `pulse::computed<double> total;`.

### `computed` as a class member

Members are initialized in declaration order, so declare the sources before the `computed`, and
initialize it in the constructor's member-initializer list:

```cpp
class Cart {
public:
  pulse::property<std::vector<Line>> lines;
  pulse::property<double> discount{0};

  pulse::computed<double> subtotal;
  pulse::computed<double> total;

  Cart()
      : subtotal([this] {
          double s = 0;
          for (const Line& l : lines.get()) s += l.price * l.qty;
          return s;
        }, lines),
        total([this] { return subtotal.get() * (1 - discount.get()); }, subtotal, discount) {}
};
```

## Putting it together: a view model

```cpp
struct LoginModel {
  pulse::property<std::string> email, password;
  pulse::property<bool> busy{false};
  pulse::signal<std::string> error;
  pulse::signal<> logged_in;

  pulse::computed<bool> can_submit;

  LoginModel()
      : can_submit([this] {
          return !busy.get() && email.get().find('@') != std::string::npos && password.get().size() >= 8;
        }, email, password, busy) {}

  void submit() {
    if (!can_submit.get()) return;
    busy = true;
    auth_async(email.get(), password.get(), [this](bool ok, std::string why) {
      busy = false;
      if (ok) logged_in(); else error(why);
    });
  }
};
```

A UI binds its text fields to `email` and `password`, binds the button's enabled state to
`can_submit`, shows a spinner from `busy`, and connects `error` to a message box. The model knows
nothing about the UI and is trivial to unit-test:

```cpp
LoginModel m;
m.email = "a@b.c";
m.password = "12345678";
assert(m.can_submit.get());
```

Next: [Hot sources](06-hot-sources.md).
