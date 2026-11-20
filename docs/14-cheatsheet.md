# 14. Cheat sheet

```cpp
#include <pulse/pulse.hpp>
using namespace std::chrono_literals;
```

## Object interaction

```cpp
pulse::signal<int> sig;                        // pulse::signal<> for no arguments
auto c = sig.connect([](int v) {});            // or connect([]{}), connect(obj, &T::m), connect(ex, f)
sig.emit(1);  sig(1);                          // emit
c.disconnect();  c.connected();
pulse::subscription s = c.scoped();            // RAII
pulse::scoped_connection sc = sig.connect(f);  // RAII
sig.set_blocked(true);  sig.slot_count();  sig.disconnect_all();
sig.as_observable();                           // observable<int>
pulse::emit_into(obs, sig);                    // observable -> signal

pulse::property<T> p{init};
p.get();  p();  T v = p;                       // copies
p = v;  p.set(v);                              // emits only if changed (needs operator==)
p.update([](T& v) { ... });
p.changed.connect(f);  p.on_change(f);         // future changes
p.as_observable();                             // current + changes
p.map(f);                                      // current + changes, transformed
p.bind(obs);  p.bind(other);  p.bind(other, f);  p.unbind();  p.is_bound();
auto link = p.bind_bidirectional(other);

pulse::computed<R> c([&] { ... }, src1, src2); // or CTAD: pulse::computed c(...)
c.get();  c.changed;  c.as_observable();  c.map(f);  c.recompute();
```

## Hot sources

```cpp
pulse::subject<T> s;            s.on_next(v); s.on_error(e); s.on_completed(); s.as_observable();
pulse::behavior_subject<T> b{init};   b.value();  // replays the latest to new subscribers
pulse::topic<T> t;              t.publish(v);  t(v);
t.subscribe(f);
t.subscribe(ex, f);
t.subscribe(ex, pulse::priority{10}, pulse::bp_latest<T>{}, f);
pulse::as_observable(t);  pulse::as_observable(t, ex);
```

## Cold sources

```cpp
pulse::just(a, b, c);   pulse::from(container);   pulse::from({1, 2, 3});   pulse::range(0, 10);
pulse::empty<T>();      pulse::never<T>();        pulse::error<T>(std::runtime_error("x"));
pulse::defer([] { return make_observable(); });
pulse::timer(500ms, ex);      pulse::interval(1s, ex /*, initial_delay*/);
pulse::observable<T>::create([](auto on_next, auto on_error, auto on_done) { return pulse::subscription{...}; });
```

## Subscribing and lifetime

```cpp
auto s = obs.subscribe(on_next, on_error, on_done);   // keep s!
s.reset();  s.active();  s.release();  s.cancel_on_destruct(false);
pulse::subscription_bag bag;  bag += obs.subscribe(f);  bag.clear();  bag.reset();
```

## Operators

| Group | Operators |
|---|---|
| transform | `map(f)` `scan(seed, f)` `pairwise()` `buffer(n)` `window(n)` |
| filter | `filter(p)` `distinct_until_changed([key])` `distinct()` `skip(n)` `skip_while(p)` `take(n)` `take_while(p)` `take_until(obs)` `first()` `last()` |
| side effects | `tap(f)` `finally_do(f)` |
| aggregate | `reduce(seed, f)` `start_with(v)` `default_if_empty(v)` |
| time | `debounce(d, ex)` `throttle(d)` `throttle_latest(d, ex)` `delay(d, ex)` `timeout(d)` |
| higher-order | `switch_map(f)` `concat_map(f)` `flat_map(f)` / `merge_map(f)` |
| combine | `merge(a, b, ...)` `combine_latest(a, b, [c, [d,]] f)` `with_latest_from(obs, f)` `zip(a, b, f)` |
| errors | `catch_error(h)` `on_error_return(v)` `retry(k)` |
| threads | `observe_on(ex)` `subscribe_on(ex)` |
| sharing | `share()` `publish(src)` + `.connect()` `ref_count(conn[, grace])` |

## Executors and time

```cpp
pulse::inline_exec();                  // run immediately
pulse::strand st;  st.drain();          // queue you drain yourself
pulse::thread_pool pool{4};
struct my_exec : pulse::executor { void post(std::function<void()> f) override; };

pulse::post_delayed(ex, 3s, fn);                         // -> subscription
pulse::timer_service::instance().schedule_after(1s, fn); // runs on the timer thread
```

## Backpressure policies (topic)

`bp_none{}` · `bp_drop{n}` · `bp_latest<T>{}` · `bp_buffer<T>{cap}` · `bp_buffer_n<T, N>` ·
`bp_batch_n<T, N>` · `bp_batch_count_or_timeout_nms<T, N, Ms>`

## Decision guide

| Need | Tool |
|---|---|
| "Tell me when X happens" | `signal` |
| "Hold X and tell me when it changes" | `property` |
| "X is derived from Y and Z" | `computed` |
| "Push values into a pipeline" | `subject` |
| "App-wide events across threads" | `topic` |
| "Timing, cancellation, combining" | operators |
| "Run on the UI thread" | `observe_on(ui)` or `connect(ui, f)` |
