# 8. Threads and time

## The threading model in one paragraph

Pulse does not create threads behind your back, apart from **one** timer thread. A value travels
through a pipeline **synchronously, on the thread that produced it**, until it reaches a point where
you explicitly asked for another thread: `observe_on`, `subscribe_on`, a queued signal connection, a
topic subscriber with an executor, or a time-based operator, which delivers on the executor you
passed. This makes behaviour predictable: if you did not ask for a thread hop, there is none.

## Executors

An executor is anything that can run a function "somewhere":

```cpp
struct executor {
  virtual ~executor() = default;
  virtual void post(std::function<void()> f) = 0;
};
```

Pulse ships four implementations.

| Executor | Runs tasks | Order | Use for |
|---|---|---|---|
| `inline_executor` / `inline_exec()` | immediately, inside `post` | caller's order | "no thread hop", tests |
| `strand` | when you call `drain()` | FIFO | single-threaded loops you drive yourself; deterministic tests |
| `thread_pool{n}` | on `n` worker threads | **unordered** across workers | CPU work, blocking I/O |
| your own | wherever you want | your choice | UI threads, game loops, Qt, ASIO... |

### `inline_exec()`

A process-wide `inline_executor`. Passing it means "run on whatever thread delivers the value".

### `strand`

A queue without its own thread. `post` enqueues; `drain()` runs everything queued, including tasks
posted while draining, and returns how many it ran.

```cpp
pulse::strand loop;
auto s = (pulse::interval(16ms, loop) | pulse::map(update_physics)).subscribe(render);

while (running) {
  loop.drain();            // ticks and their pipelines run here, on this thread
  wait_for_vsync();
}
```

Strands are excellent for tests: post work, `drain()`, assert. No sleeping, no races.

### `thread_pool`

```cpp
pulse::thread_pool pool;        // std::thread::hardware_concurrency() workers
pulse::thread_pool io{4};       // 4 workers
```

The destructor finishes queued tasks and joins the workers. A pool does not preserve ordering
between tasks, because they run on different workers. Do not use a multi-threaded pool with
`observe_on` when order matters; use a strand or the UI executor after the pool.

### Writing an executor for your event loop

Integrating with any loop takes a few lines. For example, with a hypothetical GUI toolkit:

```cpp
struct gui_executor final : pulse::executor {
  void post(std::function<void()> f) override {
    gui::invoke_later(std::move(f));       // the toolkit's "run on the UI thread" function
  }
};
```

PulseUI provides one ready-made executor: `pulseui::App::ui()`. The Qt adapter provides
`pulse::qt::qt_executor` (see [Qt adapter](12-qt-adapter.md)).

## Moving work between threads

### `observe_on(ex)`: where downstream code runs

Everything **after** `observe_on` in the pipeline, including your `subscribe` callbacks, runs on `ex`:

```cpp
auto s = (sensor_stream                      // emits on the sensor thread
          | pulse::map(parse)                // runs on the sensor thread
          | pulse::observe_on(ui)            // hop
          | pulse::map(format))              // runs on the UI thread
             .subscribe(update_label);       // runs on the UI thread
```

### `subscribe_on(ex)`: where the subscription starts

`subscribe_on` performs the `subscribe` call on `ex`. For a cold, synchronous source, that is where
the source does its work:

```cpp
auto contents = pulse::defer([path] { return pulse::just(read_whole_file(path)); })   // blocking
              | pulse::subscribe_on(pool)    // read on the pool
              | pulse::observe_on(ui);       // deliver on the UI thread
```

`subscribe_on` affects only the start of the stream. Hot sources emit on their own threads no matter
where you subscribed.

### Queued signal connections

```cpp
worker.progress.connect(ui, [](double p) { bar.set(p); });   // posted to ui on each emit
```

### Lifetime of executors

`observe_on(ex)`, `subscribe_on(ex)`, `timer(d, ex)`, `interval(p, ex)`, `debounce(d, ex)`,
`delay(d, ex)`, `throttle_latest(w, ex)` and `post_delayed(ex, ...)` hold a **reference** to `ex`. The
executor must outlive every subscription that uses it. Declare executors before the subscriptions
that use them, or use the `std::shared_ptr<executor>` overloads of `observe_on` / `subscribe_on`.

## Time

### The timer service

All time-based features (`timer`, `interval`, `debounce`, `throttle`, `throttle_latest`, `delay`,
`timeout`, `ref_count` grace periods, `post_delayed`, the batch-with-timeout backpressure policy)
share **one** background thread: `pulse::timer_service::instance()`.

Why one thread? A naive implementation spawns a thread (or a sleeping task) per debounce, per timer
or per keystroke. That wastes resources and makes cancellation racy. The timer service keeps a
priority queue of deadlines, sleeps until the next one, runs it, and supports cancellation of
pending and running tasks.

Timer callbacks run **on the timer thread**. The built-in operators immediately hop to your executor,
so your code never runs there. If you use the service directly, keep callbacks short and post real
work to an executor.

```cpp
auto& ts = pulse::timer_service::instance();

pulse::subscription once  = ts.schedule_after(500ms, [] { /* on the timer thread */ });
pulse::subscription at    = ts.schedule_at(std::chrono::steady_clock::now() + 2s, [] { ... });
pulse::subscription every = ts.schedule_periodic(0ms, 100ms, [] { ... });   // fixed rate
ts.pending();   // number of scheduled tasks
```

Periodic tasks run at a fixed rate. If the thread falls behind (a callback took too long), missed
ticks are **skipped**, not replayed in a burst.

You can create additional `timer_service` instances if you want an isolated timer thread, but the
built-in operators always use `instance()`.

### `post_delayed(ex, d, fn)`

Runs `fn` on executor `ex` after `d`. Cancel with the returned subscription. Cancellation also
prevents a task that is already queued on `ex` from running.

```cpp
pulse::subscription hide = pulse::post_delayed(ui, 3s, [&] { toast.hide(); });
// user hovered the toast: keep it
hide.reset();
```

### Time-based sources

```cpp
pulse::timer(500ms, ui)                 // observable<int>: 0 after 500 ms, then completes
pulse::interval(1s, ui)                 // observable<size_t>: 0, 1, 2, ... every second
pulse::interval(1s, ui, 5000ms)         // first tick after 5 s
```

### Testing time-based code

Time-based operators use real time. To keep tests fast and deterministic:

- use short durations (5–20 ms) and generous assertions (wait for "at least", not "exactly");
- deliver on a `strand` and `drain()` it in a loop with a timeout;
- or, better, keep timing at the edges: test your `map`/`scan` logic with `just`/`from`, and test the
  timing operators separately.

```cpp
pulse::strand s;
std::vector<int> got;
auto sub = (pulse::just(1, 2, 3) | pulse::delay(10ms, s)).subscribe([&](int v) { got.push_back(v); });

auto deadline = std::chrono::steady_clock::now() + 1s;
while (got.size() < 3 && std::chrono::steady_clock::now() < deadline) {
  s.drain();
  std::this_thread::sleep_for(1ms);
}
assert((got == std::vector<int>{1, 2, 3}));
```

## Thread-safety summary

| Type | Thread-safe | Callbacks run on |
|---|---|---|
| `signal` | yes: connect, disconnect, emit | the emitter (direct) or the slot's executor (queued) |
| `property`, `computed` | yes: get, set, update | the thread that called `set` |
| `subject`, `behavior_subject` | yes | the thread that called `on_next` |
| `topic` | yes | each subscriber's executor |
| `subscription::reset` | yes, from any thread, idempotent | n/a |
| `composite_subscription` | yes | n/a |
| `observable` (the object) | immutable, freely copyable and shareable | depends on the source and operators |

Next: [Backpressure](09-backpressure.md).
