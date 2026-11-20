# 9. Backpressure

**Backpressure** is what happens when a producer emits faster than a consumer can handle. Without a
plan, queues grow without limit (memory), latency grows (the UI shows data from 10 seconds ago), or
work is wasted on values that are already outdated.

Pulse handles this in two places:

1. **Operators** for streams: `debounce`, `throttle`, `throttle_latest`, `buffer`, `distinct_until_changed`
   and `sample`-like patterns built from them (see [Operators](07-operators.md#time)).
2. **Policies** for `topic` subscribers: every subscriber chooses what happens to events that
   arrive while it is busy.

This chapter is about the second.

## Policies

Pass a policy when subscribing to a topic:

```cpp
topic.subscribe(executor, pulse::priority{0}, POLICY, handler);
```

| Policy | Behaviour | Typical use |
|---|---|---|
| `bp_none{}` (default) | every event is posted to the executor | low-rate events; the handler keeps up |
| `bp_drop{n}` | accepts the first `n` events for this subscriber, then drops all later ones | "first N" quotas, sampling at startup |
| `bp_latest<T>{}` | coalesces: at most one pending task; when it runs, it handles the newest value | UI state, positions, progress: only the latest matters |
| `bp_buffer<T>{cap}` | a bounded FIFO of `cap` (default 64); a single worker task drains it; when full, **new** events are dropped and counted | logs, commands: keep order, bound memory |
| `bp_buffer_n<T, N>` | the same with a compile-time capacity | as above, no heap configuration |
| `bp_batch_n<T, N>` | waits until `N` events have accumulated, then runs the handler for all `N` in one task | reducing per-event posting overhead |
| `bp_batch_count_or_timeout_nms<T, N, Ms>` | like `bp_batch_n`, but flushes whatever has accumulated `Ms` milliseconds after the first event of a batch | batching with a latency bound (telemetry, network writes) |

### How they work

Policies with a `publish(value, executor, invoke)` member (`bp_latest`, the buffers and the batches)
control scheduling themselves. Typically they keep one in-flight worker task that drains an internal
queue, so a burst of 10,000 events costs a handful of posts instead of 10,000.

Simple policies with `accept()` (`bp_none`, `bp_drop`) only decide whether each event is posted.

### Examples

```cpp
pulse::topic<MousePos> mouse;

// Redraw the crosshair with the newest position only; never fall behind.
auto draw = mouse.subscribe(ui, pulse::priority{0}, pulse::bp_latest<MousePos>{},
                            [](const MousePos& p) { crosshair.move_to(p); });

// Record every position for a replay file, in order; at most 10k queued, drop beyond that.
auto rec = mouse.subscribe(disk_pool, pulse::priority{0}, pulse::bp_buffer<MousePos>{10'000},
                           [](const MousePos& p) { recorder.write(p); });

// Send telemetry in groups of 50, or every 200 ms, whichever comes first.
auto tel = mouse.subscribe(net_pool, pulse::priority{0},
                           pulse::bp_batch_count_or_timeout_nms<MousePos, 50, 200>{},
                           [](const MousePos& p) { telemetry.add(p); });
```

### Writing your own policy

A policy is any type with either:

```cpp
bool accept();                                             // simple: post or drop
```

or

```cpp
template <class Executor, class Invoke>
void publish(const T& v, Executor& ex, Invoke invoke);     // full control: call ex.post(...) and invoke(value)
```

Each subscription gets its own copy of the policy (moved or copied; default-constructed if neither
is possible). `publish` may be called concurrently from several publishing threads, so protect your
state. Look at `bp_latest` in `include/pulse/core/backpressure.hpp` for a compact reference
implementation.

## Backpressure in stream pipelines

Streams (`observable`) are push-based without demand signalling. To stay in control:

- **Coalesce** with `throttle_latest` (steady updates and the final value) or `debounce` (only the
  final value).
- **Drop** with `throttle` (the leading value only) or `filter`.
- **Batch** with `buffer(n)`.
- **Bound concurrency** of async work with `switch_map` (one at a time, latest wins) or `concat_map`
  (one at a time, all of them), instead of `flat_map` (unbounded).
- **Move slow consumers** to their own executor with `observe_on`, and remember that an executor's
  queue is unbounded. Combine it with one of the above.

Next: [Errors and completion](10-errors.md).
