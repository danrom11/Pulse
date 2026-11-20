# 6. Hot sources: subject, behavior_subject, topic

Cold observables generate values on their own. **Hot sources** are fed by *your* code: you push values
in, and every current subscriber receives them. They are the bridge between imperative code
("the socket received a message") and reactive pipelines.

| Type | Remembers a value | Terminal events | Delivery | Best for |
|---|---|---|---|---|
| `subject<T>` | no | yes (`on_error`, `on_completed`) | synchronous, on the caller | Turning callbacks into a stream |
| `behavior_subject<T>` | the latest | yes | synchronous | "Current state" streams (like `property`, but with completion) |
| `topic<T>` | no | no | per subscriber: any executor, priority, backpressure | Event buses between subsystems and threads |
| `signal<T>` | no | no | synchronous or queued per slot | Object events (see [chapter 5](05-signals-and-properties.md)) |
| `property<T>` | the current | no | synchronous | State (see [chapter 5](05-signals-and-properties.md)) |

## `pulse::subject<T>`

A hot observable that you control.

```cpp
pulse::subject<std::string> lines;

auto s = (lines.as_observable() | pulse::filter(is_error)).subscribe(log_error);

lines.on_next("starting");       // to every current subscriber, synchronously
lines.on_next("error: disk full");
lines.on_completed();            // terminal: subscribers get on_completed, later on_next is ignored
```

| Member | Meaning |
|---|---|
| `as_observable()` | The stream view; subscribe to this or pass it to others |
| `subscribe(on_next, on_err, on_done)` | Shorthand for `as_observable().subscribe(...)` |
| `on_next(v)` / `operator()(v)` | Emit a value to all current subscribers |
| `on_error(exception_ptr)` | Terminate with an error |
| `on_completed()` | Terminate normally |
| `has_observers()` | Whether anyone is subscribed |

Semantics:

- Subscribers receive only values emitted **after** they subscribed.
- After `on_completed` / `on_error`, new subscribers immediately receive that terminal event, and
  `on_next` does nothing.
- Thread-safe: you may call `on_next` from several threads. Values are delivered on the calling
  thread, so subscriber callbacks can run concurrently in that case. Serialize producers, or add
  `observe_on(strand_or_ui)`, if your subscriber is not thread-safe.
- Subscriptions and `as_observable()` copies may outlive the subject.

### Pattern: expose the stream, hide the subject

Keep the subject private so only the owner can emit:

```cpp
class Socket {
public:
  pulse::observable<Message> messages() const { return messages_.as_observable(); }
private:
  void on_data(const Message& m) { messages_.on_next(m); }   // called by the network layer
  pulse::subject<Message> messages_;
};
```

## `pulse::behavior_subject<T>`

A subject that remembers the latest value and gives it to every new subscriber immediately.

```cpp
pulse::behavior_subject<ConnectionState> state{ConnectionState::Disconnected};

auto a = state.subscribe(print);        // prints Disconnected immediately
state.on_next(ConnectionState::Connecting);
auto b = state.subscribe(print);        // prints Connecting immediately
state.value();                          // Connecting (a copy)
```

It has the same members as `subject`, plus `value()`.

**`behavior_subject` or `property`?** Both hold a current value. Choose `property` for object state:
it has `set`/`get`, equality filtering, bindings and works with `computed`. Choose `behavior_subject`
when you need stream semantics: completion or errors, or emitting equal values repeatedly (it does
not filter duplicates).

## `pulse::topic<T>`: the event bus

`topic` is a thread-safe publish/subscribe bus in which **each subscriber chooses how it receives
events**:

- on which **executor** its handler runs (inline, a thread pool, the UI thread, a strand);
- its **priority** (higher runs first);
- its **backpressure policy** (what happens when events arrive faster than the handler runs).

```cpp
pulse::topic<Order> orders;
pulse::thread_pool workers{4};

// UI: on the UI executor, only the latest order matters for the "last order" label
auto ui = orders.subscribe(ui_executor, pulse::priority{0}, pulse::bp_latest<Order>{},
                           [](const Order& o) { last_order_label.set(o.id); });

// Audit: first, synchronously, never drop anything
auto audit = orders.subscribe(pulse::inline_exec(), pulse::priority{100},
                              [](const Order& o) { audit_log.write(o); });

// Pricing: on the worker pool, at most 256 queued orders, drop the rest
auto pricing = orders.subscribe(workers, pulse::priority{0}, pulse::bp_buffer<Order>{256},
                                [](const Order& o) { reprice(o); });

orders.publish(Order{...});         // or orders(Order{...})
```

### Subscribing

```cpp
subscription subscribe(Fn&& fn);                                    // inline, priority 0, no policy
subscription subscribe(executor& ex, Fn&& fn);
subscription subscribe(executor& ex, priority p, Fn&& fn);
subscription subscribe(executor& ex, priority p, BP policy, Fn&& fn);
```

- `priority{n}`: handlers are called in order of decreasing priority; equal priorities keep
  subscription order. With asynchronous executors this is the order in which work is *posted*.
- The policy is copied per subscription. See [Backpressure](09-backpressure.md) for all policies.
- The returned `subscription` unsubscribes. After that, deliveries that were already queued on an
  executor are skipped.

### Publishing

```cpp
void publish(const T& value);
void operator()(const T& value);
std::size_t subscriber_count() const;
```

`publish` may be called from any thread, and from inside a handler (re-entrantly). It never blocks on
slow subscribers. Asynchronous subscribers receive a posted task; synchronous ones run inside
`publish`.

### Topic as an observable

To use operators on a topic, convert it:

```cpp
pulse::observable<T> as_observable(topic<T>& t);                 // inline delivery
pulse::observable<T> as_observable(topic<T>& t, executor& ex);   // delivered on ex
```

```cpp
auto big_orders = pulse::as_observable(orders, ui_executor)
                | pulse::filter([](const Order& o) { return o.total > 1000; });
```

The observable keeps a reference to the topic: the topic must outlive subscribing to it.
Subscriptions themselves may outlive it.

### Topic or signal?

Both broadcast events. Use `signal` for object-level events with direct, synchronous slots ("this
button was clicked"). Use `topic` for application-level buses where subscribers live on different
threads, need ordering by priority, or need flow control.

## Bridging callbacks APIs

Most C and C++ libraries report events through callbacks. Wrap them once in `observable::create` or
a subject, and the rest of your code can use operators.

```cpp
// A callback-based API:
//   int  watch_file(const char* path, void (*cb)(const char*, void*), void* user);
//   void unwatch(int handle);

pulse::observable<std::string> file_changes(std::string path) {
  return pulse::observable<std::string>::create([path](auto on_next, auto, auto) {
    auto fn = new std::function<void(const std::string&)>(on_next);
    int h = watch_file(path.c_str(),
                       [](const char* p, void* user) { (*static_cast<std::function<void(const std::string&)>*>(user))(p); },
                       fn);
    return pulse::subscription([h, fn] { unwatch(h); delete fn; });
  });
}

auto s = (file_changes("config.json") | pulse::debounce(200ms, ui)).subscribe(reload_config);
```

Next: [Operators reference](07-operators.md).
