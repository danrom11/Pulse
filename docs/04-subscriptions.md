# 4. Subscriptions and lifetime

Every long-lived connection in Pulse is represented by a **`pulse::subscription`**: the result of
`observable::subscribe`, `topic::subscribe`, `post_delayed`, `timer_service::schedule_*`, and
(via `connection::scoped()`) of signal connections.

Getting lifetimes right is the single most important skill with any reactive library. The good news
is that in Pulse it is plain RAII.

## `pulse::subscription`

```cpp
class subscription {
public:
  subscription();                                         // empty
  explicit subscription(std::function<void()> cancel, bool cancel_on_dtor = true);

  subscription(subscription&&);  subscription& operator=(subscription&&);   // move-only
  ~subscription();                                         // cancels (unless detached)

  void reset();            // cancel now (idempotent); also: unsubscribe()
  void release();          // forget the cancel function: the stream keeps running
  std::function<void()> take();   // hand the cancel function out
  bool active() const;     // has a cancel function; also explicit operator bool
  subscription& cancel_on_destruct(bool);   // false = destructor does not cancel
  void swap(subscription&);
};
```

Behaviour in detail:

| Operation | Effect |
|---|---|
| Destructor | Cancels, unless `cancel_on_destruct(false)` was set or `release()` was called |
| `reset()` / `unsubscribe()` | Cancels exactly once; later calls do nothing; never throws |
| Move-assign onto an active subscription | Cancels the old one first, then takes the new one |
| `release()` | Drops the cancel function. The stream runs until it completes on its own |
| `cancel_on_destruct(false)` | Keeps the cancel function (you can still `reset()`), but destruction will not cancel |
| `take()` | Returns the cancel function and leaves the subscription empty |

Subscriptions are **move-only**: one stream, one owner. To keep many, use a container or a
[bag](#composite_subscription--subscription_bag).

Cancelling is safe:

- from any thread;
- from inside the stream's own callback (`on_next` may `reset()` its own subscription);
- after the source is gone (a no-op).

## The three lifetime patterns

### 1. A member: lives as long as the object

The most common case. The object starts a subscription in its constructor and stores it; when the
object dies, the subscription cancels and no callback can reach the dead object.

```cpp
class StatusBar {
public:
  explicit StatusBar(pulse::observable<std::string> messages)
      : sub_(messages.subscribe([this](const std::string& m) { text_ = m; })) {}
private:
  std::string text_;
  pulse::subscription sub_;   // declared after the state it touches
};
```

Capturing `this` is safe **because** `sub_` is a member: it cannot outlive the object.

> **Member order matters.** Members are destroyed in reverse declaration order. Declare
> subscriptions *after* the data their callbacks use, so they are cancelled first.

### 2. A scope: lives for a block

```cpp
{
  auto sub = progress.subscribe(update_bar);
  run_job();                 // progress values arrive while the job runs
}                            // sub destroyed: no more updates
```

### 3. Fire and forget: lives until the stream ends

For finite streams that should run to completion even if nobody holds them:

```cpp
(pulse::timer(2s, ui) | pulse::map([](int) { return "ready"; }))
    .subscribe([](const char* m) { show(m); })
    .release();               // do not cancel at the end of the statement
```

Only do this for streams that **do** end (timers, finite requests). Releasing an infinite stream
leaks it forever, and its callbacks keep running.

## `composite_subscription` / `subscription_bag`

A thread-safe container that cancels everything it holds together.

```cpp
class Controller {
public:
  Controller(Model& m) {
    bag_ += m.name.as_observable().subscribe([this](const std::string& n) { title_ = n; });
    bag_ += m.items.as_observable().subscribe([this](const auto& v) { count_ = v.size(); });
    bag_.add(m.saved.connect([this] { dirty_ = false; }));     // a connection converts to a subscription
  }
private:
  std::string title_;
  std::size_t count_ = 0;
  bool dirty_ = false;
  pulse::subscription_bag bag_;      // destroyed first: everything is cancelled
};
```

| Member | Effect |
|---|---|
| `add(s)` / `operator+=(s)` | Store a subscription. If the bag was already `reset()`, `s` is cancelled immediately |
| `clear()` | Cancel everything, keep accepting new subscriptions (for example on "reload") |
| `reset()` | Cancel everything and **close** the bag; later additions are cancelled immediately |
| `size()` | Number of stored subscriptions |
| `is_cancelled()` | Whether `reset()` has been called |
| destructor | `reset()` |

`subscription_bag` is an alias of `composite_subscription`. Use whichever name reads better.

A `std::vector<pulse::subscription>` works too when you do not need thread safety or the "closed"
state.

## Connections versus subscriptions

`signal::connect` returns a **`pulse::connection`**, which behaves differently on purpose:

| | `subscription` | `connection` |
|---|---|---|
| Copyable | no (move-only) | yes |
| Destructor cancels | yes | **no** |
| How to cancel | `reset()` or destroy | `disconnect()` |
| RAII form | itself | `scoped()` → `subscription`, or `scoped_connection` |

Why the difference? Signal connections are often made "for the lifetime of the program" or for the
lifetime of an object that also owns the signal (`button.clicked.connect(...)`), where RAII would be
noise. When you *do* want RAII, convert explicitly:

```cpp
pulse::subscription s = sig.connect(slot).scoped();   // explicit
pulse::subscription t = sig.connect(slot);            // implicit conversion does the same
pulse::scoped_connection u = sig.connect(slot);       // dedicated RAII holder
```

See [Signals, properties, computed](05-signals-and-properties.md#connections).

## Lifetime rules of thumb

1. **If a callback captures `this`, the subscription must not outlive `this`.** Store it in the
   object (member or bag).
2. **If a callback captures a reference to a local, keep the subscription in the same scope.**
3. **Executors passed by reference must outlive the subscriptions that use them** (`observe_on(ex)`,
   `timer(d, ex)`, `debounce(d, ex)`). Use `observe_on(std::shared_ptr<executor>)` when you cannot
   guarantee that.
4. **Sources may die before subscriptions.** Subscriptions to a destroyed `signal`, `property`,
   `subject` or `topic` simply receive nothing more, and cancelling them is harmless.
5. **Never discard the result of `subscribe` by accident.** If you mean fire-and-forget, write
   `.release()` so readers know it is deliberate.

Next: [Signals, properties, computed](05-signals-and-properties.md).
