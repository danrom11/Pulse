# 1. Introduction

## The problem Pulse solves

Every non-trivial program has parts that need to react to each other:

- a label should show the current value of a counter;
- a search should run when the user stops typing, and only for the latest query;
- three widgets should update when the selected item changes;
- a background worker should report progress to the UI thread;
- a reconnecting client should retry with a delay, and give up after five attempts.

The classic approach is callbacks plus flags plus timers: `bool dirty`, `int pending_requests`,
`std::thread` for a delay, and a `std::vector<std::function<void()>>` somewhere. It works, but the
logic is spread across many places, and every new requirement multiplies the number of states.

Pulse gives you two small vocabularies that cover these cases.

### Vocabulary 1: object interaction

```cpp
pulse::property<int> count{0};                 // a value that tells you when it changes
pulse::signal<std::string> message;            // an event with a payload

count.changed.connect([](int v) { std::cout << "count = " << v << "\n"; });
message.connect([](const std::string& m) { log(m); });

count = 5;               // prints "count = 5"
message("saved");        // calls log("saved")
```

If you have used Qt, this is `Q_PROPERTY` + `NOTIFY` and signals/slots, with no `moc`, no `QObject`
base class and no macros. Any object can have signals and properties; they are ordinary members.

### Vocabulary 2: streams over time

```cpp
auto results = query_changes                        // observable<std::string>
             | pulse::debounce(300ms, ui)           // wait until typing pauses
             | pulse::distinct_until_changed()      // skip if the text did not change
             | pulse::switch_map(search_async);     // cancel the previous search

auto sub = results.subscribe([](const Results& r) { show(r); });
```

An `observable<T>` describes values that arrive over time. Operators such as `debounce` and
`switch_map` transform one stream into another. The pipeline is the whole specification: there is no
hidden flag or timer anywhere else.

## The mental model

```
   producers                         pipeline                        consumers
 ┌────────────┐                ┌───────────────────┐              ┌───────────────┐
 │ property   │──as_observable─►                   │              │               │
 │ signal     │──as_observable─►  map | filter |   ├──subscribe──►│ callback,     │
 │ subject    │──as_observable─►  debounce | ...   │              │ property.bind,│
 │ topic      │──as_observable─►                   │              │ signal        │
 │ just/from  │────────────────►                   │              │               │
 └────────────┘                └───────────────────┘              └───────────────┘
                                         │
                              subscription (RAII): destroy it and
                              the whole chain is torn down
```

Three ideas hold everything together:

1. **An observable is a recipe, not a running process.** Building `a | map(f) | filter(g)` does
   nothing. Work starts when you call `subscribe`, and each `subscribe` call starts its own
   independent execution of the recipe (see [cold and hot](03-observables.md#cold-and-hot)).
2. **A subscription owns the running process.** `subscribe` returns a `pulse::subscription`. When it
   is destroyed, the stream is cancelled: timers stop, inner requests are dropped and callbacks stop
   arriving. Lifetime is ordinary C++ scope.
3. **Executors decide where callbacks run.** By default everything is synchronous, on the thread that
   produced the value. When you need another thread, or the UI thread, you say so explicitly with
   `observe_on(executor)` or a queued `connect(executor, ...)`.

## Which tool do I use?

| You want to... | Use | Why |
|---|---|---|
| Notify listeners that something happened | `signal<Args...>` | Cheapest; synchronous; no stream semantics needed |
| Hold state that others display or depend on | `property<T>` | Stores the value; emits only on real changes; can be bound |
| Hold state derived from other state | `computed<R>` | Recomputes automatically; never goes stale |
| Push values into a pipeline from imperative code | `subject<T>` | A hot observable you can `on_next` into |
| Same, but late subscribers need the current value | `behavior_subject<T>` | Replays the latest value on subscribe |
| A many-to-many event bus with threads, priorities, flow control | `topic<T>` | Per-subscriber executor, priority and backpressure |
| Describe timing: debounce, throttle, timeout, delay, retry | operators on `observable<T>` | Timing logic stays in one declarative place |
| Combine several sources | `combine_latest`, `with_latest_from`, `merge`, `zip` | Stateful combination without manual flags |
| Run one expensive source for many listeners | `share()` / `publish` + `ref_count` | One upstream subscription, many downstream |

A good rule of thumb: **use the simplest tool that works.** If a callback is enough, use a
`signal`. If others need to read the value later, use a `property`. Reach for observables when time,
cancellation or combination is involved. They are very good at that, and too much machinery for
"call me when X happens".

## Design principles

Pulse follows a few decisions, and knowing them makes the library predictable.

- **Header-only, zero dependencies, C++20.** Add an include path and you are done. Nothing to build
  or link except the system threads library for timers.
- **Synchronous by default.** A value travels through a pipeline on the producer's thread unless you
  ask otherwise. This makes ordering obvious and debugging easy (one call stack).
- **Explicit threading.** Thread hops only happen in `observe_on`, `subscribe_on`, queued
  connections, time-based operators (they deliver on the executor you pass) and topic subscribers.
- **Everything is cancellable.** Every subscription can be cancelled from any thread, at any moment,
  including from inside its own callback.
- **Thread-safe primitives.** `signal`, `property`, `subject`, `behavior_subject`, `topic` and
  `composite_subscription` can be used from several threads at once.
- **Re-entrancy is allowed.** A slot may emit, connect or disconnect while it is being called; a
  topic handler may publish again. The primitives work on snapshots, so this cannot deadlock or
  invalidate iterators.
- **Lifetimes are forgiving.** Connections and subscriptions may outlive the signal, property or
  subject they came from; cancelling them later is a harmless no-op.
- **One timer thread.** All time-based operators share a single `timer_service` thread instead of
  creating threads per event.

Next: [Getting started](02-getting-started.md).
