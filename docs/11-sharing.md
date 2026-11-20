# 11. Sharing one source

## The problem

Cold observables run once **per subscriber**. Usually that is what you want, but it is wasteful
when the source is expensive or has side effects:

```cpp
auto prices = pulse::interval(1s, pool) | pulse::switch_map(fetch_prices);   // an HTTP request per tick

auto a = prices.subscribe(update_table);      // request stream #1
auto b = prices.subscribe(update_chart);      // request stream #2: twice the traffic
```

Sharing makes one upstream subscription serve many downstream subscribers.

## `share()`

```cpp
auto prices = pulse::interval(1s, pool) | pulse::switch_map(fetch_prices) | pulse::share();

auto a = prices.subscribe(update_table);   // starts the source
auto b = prices.subscribe(update_chart);   // joins the running source
a.reset();
b.reset();                                 // the last subscriber left: the source stops
auto c = prices.subscribe(log_prices);     // starts it again
```

Rules:

- The **first** subscriber starts the upstream; the **last** one to leave stops it.
- Subscribers that join later receive only values emitted after they joined.
- Completion and errors are fanned out to everyone. A subscriber that arrives **after** the source
  terminated receives the same terminal event immediately.

Two equivalent spellings: `src | pulse::share()` and `pulse::share(src)`.

## `publish()` and `connect()`

Sometimes you need control over **when** the shared source starts, for example to attach all
subscribers first so nobody misses the first values of a synchronous source.

```cpp
pulse::connectable_observable<Event> events = pulse::publish(read_all_events());

auto a = events.as_observable().subscribe(index);      // attached, nothing flows yet
auto b = events.as_observable().subscribe(display);

pulse::subscription running = events.connect();        // now the upstream runs, once, for both
// ...
running.reset();                                       // disconnect the upstream
```

| Member | Meaning |
|---|---|
| `as_observable()` | The hot view to subscribe to |
| `connect()` | Start the upstream if it is not running; returns a subscription that disconnects it. Calling it again while running returns an empty subscription |

## `ref_count(conn)` and `ref_count(conn, grace)`

`ref_count` automates `connect`: the upstream connects when the first subscriber arrives and
disconnects when the last one leaves. `ref_count(publish(src))` behaves like `share`.

The **grace** variant waits before disconnecting:

```cpp
auto camera = pulse::ref_count(pulse::publish(open_camera_stream()), 2s);
```

If the last subscriber leaves and a new one arrives within 2 seconds, the running upstream is
reused. This avoids the expensive stop/start cycle when, for example, a user switches between two
screens that both show the camera.

## When to share

| Situation | Share? |
|---|---|
| The source performs network or disk I/O, or heavy computation | yes |
| The source has side effects (sends, writes, logs) that must happen once | yes |
| The source is already hot (`subject`, `topic`, `property`) | no need: hot sources are shared by nature |
| The pipeline is cheap (`map`, `filter` over a hot source) | not worth it |
| Each subscriber must get the full sequence from the beginning | no: sharing gives late subscribers only the rest |

Next: [Qt adapter](12-qt-adapter.md).
