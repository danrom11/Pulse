# 7. Operators reference

Operators transform observables. They are used with `|` and are all in `namespace pulse`.

```cpp
auto out = source | op1(args) | op2(args);
```

Every operator:

- returns a new **cold** observable (nothing runs until `subscribe`);
- keeps **per-subscription state** (subscribing twice gives two independent executions);
- forwards errors downstream immediately unless it says otherwise;
- cancels its upstream (and any inner subscriptions or timers) when the downstream unsubscribes.

Contents:

- [Transforming](#transforming): `map`, `scan`, `pairwise`, `buffer`, `window`
- [Filtering](#filtering): `filter`, `distinct_until_changed`, `distinct`, `skip`, `skip_while`, `take`, `take_while`, `take_until`, `first`, `last`
- [Side effects and aggregation](#side-effects-and-aggregation): `tap`, `finally_do`, `reduce`, `start_with`, `default_if_empty`
- [Time](#time): `debounce`, `throttle`, `throttle_latest`, `delay`, `timeout`, plus the sources `timer` and `interval`
- [Higher-order (streams of streams)](#higher-order-operators): `switch_map`, `concat_map`, `flat_map` / `merge_map`
- [Combining](#combining): `merge`, `combine_latest`, `with_latest_from`, `zip`
- [Errors](#errors): `catch_error`, `on_error_return`, `retry`
- [Threads](#threads): `observe_on`, `subscribe_on`
- [Sharing](#sharing): `share`, `publish`, `ref_count`
- [Choosing the right operator](#choosing-the-right-operator)

---

## Transforming

### `map(f)`

Applies `f` to every value. The result type is whatever `f` returns.

```
source:     --1----2----3--|
map(x*10):  --10---20---30-|
```

```cpp
auto names = users | pulse::map([](const User& u) { return u.name; });   // observable<std::string>
```

If `f` throws, the exception propagates out of the producer's `on_next` call and is **not** turned
into `on_error`. Keep `f` non-throwing, or catch inside it.

### `scan(seed, acc)`

Running accumulation: emits every intermediate result of `acc(state, value)`.

```
source:            --1--2--3--|
scan(0, a+b):      --1--3--6--|
```

```cpp
auto total = purchases | pulse::scan(0.0, [](double sum, const Purchase& p) { return sum + p.price; });

// a sliding window of the last 60 values
auto last60 = samples | pulse::scan(std::vector<double>{}, [](std::vector<double> acc, double v) {
  acc.push_back(v);
  if (acc.size() > 60) acc.erase(acc.begin());
  return acc;
});
```

The accumulator type is the type of `seed`. `acc` receives the previous state and the new value
and returns the new state.

### `pairwise()`

Emits `std::pair<T, T>{previous, current}`, starting with the second value.

```
source:     --a--b--c--|
pairwise:   -----ab-bc-|
```

```cpp
auto deltas = positions | pulse::pairwise()
            | pulse::map([](const std::pair<Point, Point>& p) { return p.second - p.first; });
```

### `buffer(count)`

Collects values into `std::vector<T>` chunks of `count`. On completion, the incomplete tail is
emitted. On error, the tail is discarded and the error is forwarded.

```
source:      --1--2--3--4--5--|
buffer(2):   -----[12]--[34]--[5]|
```

```cpp
auto batches = log_lines | pulse::buffer(100);   // write 100 lines at a time
```

### `window(count)`

Like `buffer`, but each chunk is itself an `observable<T>` that emits its values as they arrive,
instead of a vector at the end. Inner observables are hot: subscribe to them inside the outer
`on_next`. Discarding the inner subscription keeps it running until its window ends.

```cpp
auto s = (source | pulse::window(3)).subscribe([](pulse::observable<int> w) {
  w.subscribe([](int v) { std::cout << v; }, {}, [] { std::cout << " | "; });
});
```

`window(0)` throws `std::invalid_argument`.

---

## Filtering

### `filter(pred)`

Passes values for which `pred(v)` is true.

```
source:           --1--2--3--4--|
filter(is_even):  -----2-----4--|
```

### `distinct_until_changed()` / `distinct_until_changed(key)`

Drops a value equal to the **previous** one (`operator==`), or whose `key(v)` equals the previous key.

```
source:                  --a--a--b--a--a--|
distinct_until_changed:  --a-----b--a-----|
```

```cpp
auto query = text_changes | pulse::distinct_until_changed();
auto by_id = selections | pulse::distinct_until_changed([](const Item& i) { return i.id; });
```

### `distinct()`

Drops every value that was **ever** seen before. Requires `std::hash<T>`, and remembers all values
seen in this subscription.

```
source:    --a--b--a--c--b--|
distinct:  --a--b-----c-----|
```

### `skip(n)` and `skip_while(pred)`

`skip(n)` drops the first `n` values. `skip_while(pred)` drops values while `pred` holds, then passes
everything (even values for which `pred` is true again).

```cpp
// property::as_observable() emits the current value first; skip it to react only to changes
auto changes_only = prop.as_observable() | pulse::skip(1);
```

### `take(n)`, `take_while(pred)`, `take_until(notifier)`

| Operator | Passes values... | Then |
|---|---|---|
| `take(n)` | the first `n` | completes and unsubscribes upstream |
| `take_while(pred)` | while `pred(v)` is true (the failing value is not emitted) | completes |
| `take_until(notifier)` | until `notifier` (any `observable<N>`) emits | completes |

```
source:              --1--2--3--4--|
take(2):             --1--2|
take_while(x < 3):   --1--2--|
```

```cpp
// stop listening when the dialog closes
auto s = (model.events() | pulse::take_until(dialog.closed.as_observable())).subscribe(handle);
```

### `first()` and `last()`

`first()` emits the first value and completes. `last()` emits the final value when the source
completes. Both error with `std::out_of_range` if the source completes without values.

---

## Side effects and aggregation

### `tap(fn)`

Calls `fn(v)` for every value and passes the value through unchanged. Use it for logging, debugging
and metrics; do not use it as a way to avoid `subscribe`.

```cpp
auto results = requests | pulse::tap([](const Request& r) { log("sending " + r.url); }) | pulse::switch_map(send);
```

### `finally_do(fn)`

Runs `fn()` exactly once when the subscription ends for **any** reason: completion, error, or
unsubscription. Ideal for "hide the spinner" or "release the resource".

```cpp
auto s = (download(url) | pulse::finally_do([&] { busy = false; })).subscribe(save);
```

### `reduce(seed, acc)`

Like `scan`, but emits only the final accumulation, when the source completes.

```
source:          --1--2--3--|
reduce(0, a+b):  -----------6|
```

### `start_with(v)`

Emits `v` first, synchronously on subscribe, then the source.

```cpp
auto status = connection_events | pulse::start_with(std::string("connecting..."));
```

### `default_if_empty(v)`

If the source completes without emitting anything, emits `v` before completing.

```cpp
auto first_match = results | pulse::take(1) | pulse::default_if_empty(Result::none());
```

---

## Time

All time-based operators run their timers on the shared [`timer_service`](08-threading-and-time.md#the-timer-service)
thread and deliver values on the executor you pass. **The executor must outlive the subscription.**

### `debounce(d, ex)`

Emits a value only after `d` has passed without a newer value. Each new value restarts the wait.
On completion, a pending value is flushed first.

```
source:          --a-b-c--------d-e------|
debounce(3 ticks): ----------c----------e-|
```

The classic use is a search box: run the query when the user stops typing.

```cpp
auto queries = text_changes | pulse::debounce(300ms, ui) | pulse::distinct_until_changed();
```

### `throttle(win)` / `throttle(win, ex)`

Emits the **first** value of a burst immediately, on the caller's thread, then ignores values until
`win` has passed. The executor argument exists for API symmetry and is not used for delivery.

```
source:           --a-b-c-----d-e-f---|
throttle(4 ticks): --a---------d-------|
```

Use it to rate-limit actions: "save at most once per second", "do not handle more than 4 scroll
events per second".

### `throttle_latest(win, ex)`

Leading **and** trailing: the first value of a burst is emitted immediately; when the window ends,
the newest value seen during the window (if any) is emitted on `ex`, which starts a new window.
The final state is never lost.

```
source:                  --a-b-c-----d------|
throttle_latest(4 ticks): --a---c-----d------|
```

Use it for progress bars, live previews, and window resizing, where you want frequent updates but
must not miss the last value.

**debounce, throttle or throttle_latest?**

| Need | Operator |
|---|---|
| Act once the burst is *over* (search, autosave after typing) | `debounce` |
| React *immediately*, then ignore the rest of the burst (button mashing) | `throttle` |
| Steady updates during the burst, and always the final value (progress, preview) | `throttle_latest` |

### `delay(d, ex)`

Shifts every value and the completion by `d`, delivered on `ex`. Errors are forwarded immediately.
Pending events are dropped on unsubscribe.

```cpp
auto toast_hide = toast_shown | pulse::delay(3s, ui);
```

### `timeout(d)`

Errors with `pulse::timeout_error` if no value arrives within `d` after subscribing, or within `d`
after the previous value. Values pass through until then.

```cpp
auto reply = request(id) | pulse::timeout(5s) | pulse::on_error_return(Reply::timed_out());
```

### Sources: `timer(due, ex)` and `interval(period, ex, initial_delay = 0ms)`

`timer` emits `0` once after `due`, then completes. `interval` emits `0, 1, 2, ...` (`std::size_t`)
every `period`, starting after `initial_delay`, until unsubscribed. Missed ticks are skipped rather
than delivered in a burst.

```cpp
auto clock = pulse::interval(1s, ui) | pulse::map([](std::size_t) { return now_string(); });
```

---

## Higher-order operators

These take a function that returns an **observable** for every outer value. They differ in what
happens when a new outer value arrives while an inner stream is still running.

```
outer:        --A-------B----|
f(A):           --a1--a2--a3|
f(B):                   --b1--b2|

switch_map:   ----a1--a2--b1--b2|      A's stream is cancelled when B arrives
concat_map:   ----a1--a2--a3--b1--b2|  B waits until A's stream completes
flat_map:     ----a1--a2-b1-a3-b2|     both run concurrently, outputs interleave
```

### `switch_map(f)`

For each value, subscribes to `f(value)` and **cancels the previous inner stream**. Completes when
the outer stream and the current inner stream have both completed.

This is the operator for "only the latest request matters": search-as-you-type, loading details of
the selected item, and auto-complete.

```cpp
auto details = selected_id | pulse::switch_map([](int id) { return load_details_async(id); });
```

Cancellation is real: the previous inner subscription is reset, so a well-written async source
stops its work and never delivers a stale result.

### `concat_map(f)`

Queues outer values and runs their inner streams **one after another**, in order. Nothing is
cancelled. Use it when order matters and every job must run: saving edits, sending messages in
sequence.

```cpp
auto saved = edits | pulse::concat_map([](const Edit& e) { return save_async(e); });
```

### `flat_map(f)` / `merge_map(f)`

Runs all inner streams **concurrently** and merges their outputs as they arrive. Completes when the
outer and all inner streams have completed. Use it for independent parallel work: download all
selected files.

```cpp
auto thumbnails = files | pulse::flat_map([&](const Path& p) { return make_thumbnail_async(p, pool); });
```

---

## Combining

### `merge(a, b, ...)`

Interleaves values from several observables of the same type as they arrive. Completes when all
sources have completed; errors on the first error.

```cpp
auto any_input = pulse::merge(keyboard_events, mouse_events, touch_events);
```

### `combine_latest(a, b, [c, [d,]] f)`

Once **every** source has emitted at least once, emits `f(latest_a, latest_b, ...)` whenever **any**
source emits. Two, three or four sources are supported. Completes when all sources complete.

```
a:               --1-----2--------3--|
b:               -----x-----y--------|
combine_latest:  -----1x-2x-2y----3y-|
```

```cpp
auto can_submit = pulse::combine_latest(email.as_observable(), password.as_observable(),
    [](const std::string& e, const std::string& p) { return valid_email(e) && p.size() >= 8; });
```

Because properties' `as_observable()` emit their current value immediately, `combine_latest` over
properties emits right away. This is the stream equivalent of `computed`.

### `with_latest_from(other, f)`

For every value of the **source**, emits `f(value, latest_other)`, but only once `other` has
emitted. Values of `other` alone emit nothing. Completion follows the source only.

```
source:            --a-----b-----c--|
other:             -----1-----2-----|
with_latest_from:  --------b1----c2-|
```

Use it when one stream is the trigger and the other is context: "on Save click, take the current
document".

```cpp
auto save_requests = save_clicks.as_observable()
                   | pulse::with_latest_from(document.as_observable(),
                                             [](pulse::unit, const Doc& d) { return d; });
```

### `zip(a, b, f)`

Pairs values **by index**: the first of `a` with the first of `b`, the second with the second, and
so on. Unpaired values are queued. Completes when one source has completed and its queue is
drained.

```cpp
auto labeled = pulse::zip(pulse::from(names), pulse::interval(500ms, ui),
                          [](const std::string& n, std::size_t) { return n; });   // one name every 500 ms
```

---

## Errors

See [Errors and completion](10-errors.md) for the full picture.

| Operator | On error |
|---|---|
| `catch_error(h)` | switches to the observable `h(exception_ptr)` returns; values before the error are kept |
| `on_error_return(v)` | emits `v`, then completes |
| `retry(k)` | resubscribes to the source up to `k` times; `retry(0)` forwards the first error |
| `finally_do(fn)` | runs `fn` (also on completion and unsubscription) |

```cpp
auto data = fetch(url)
          | pulse::retry(3)
          | pulse::catch_error([](std::exception_ptr) { return load_from_cache(); });
```

---

## Threads

See [Threads and time](08-threading-and-time.md).

### `observe_on(ex)`

Delivers `on_next`, `on_error` and `on_completed` downstream through `ex.post(...)`. Everything
**after** `observe_on` in the pipeline runs on `ex`. Order is preserved if `ex` runs tasks in
order (UI loop, strand, `inline_exec`; not a multi-threaded pool).

```cpp
auto s = (compute_on_pool() | pulse::observe_on(ui)).subscribe(update_label);   // update_label on the UI thread
```

`observe_on(std::shared_ptr<executor>)` keeps the executor alive as long as the subscription needs it.

### `subscribe_on(ex)`

Performs the **subscription** (and therefore a cold source's work, if it is synchronous) on `ex`.
Use it to move a blocking source off the calling thread.

```cpp
auto lines = read_file_blocking(path) | pulse::subscribe_on(pool) | pulse::observe_on(ui);
```

---

## Sharing

See [Sharing one source](11-sharing.md).

| Operator | Meaning |
|---|---|
| `share()` / `share(src)` | One upstream subscription for all subscribers; starts with the first, stops after the last |
| `publish(src)` | A `connectable_observable`: subscribers attach, `connect()` starts the upstream |
| `ref_count(conn)` | Auto-connect on the first subscriber, disconnect after the last |
| `ref_count(conn, grace)` | Same, but keep the upstream for `grace` after the last subscriber left |

---

## Choosing the right operator

| I want to... | Use |
|---|---|
| Transform each value | `map` |
| Keep a running total or state | `scan` |
| Compare with the previous value | `pairwise` |
| Ignore repeats | `distinct_until_changed` |
| React only to changes of a property, not its current value | `skip(1)` |
| Wait until input pauses | `debounce` |
| Limit how often something happens | `throttle` / `throttle_latest` |
| Fail if something takes too long | `timeout` |
| Run a request per value, latest wins | `switch_map` |
| Run a request per value, in order | `concat_map` |
| Run a request per value, in parallel | `flat_map` |
| Derive a value from several sources | `combine_latest` |
| Trigger with one stream, read another | `with_latest_from` |
| Process in batches | `buffer` |
| Stop on an event | `take_until` |
| Recover from errors | `catch_error`, `on_error_return`, `retry` |
| Deliver on the UI thread | `observe_on(ui)` |
| Share an expensive source | `share()` |
