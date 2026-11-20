# 10. Errors and completion

## The model

An observable ends in one of three ways:

| Ending | Callback | Meaning |
|---|---|---|
| completion | `on_completed()` | the sequence is finished normally (`just`, `take(n)`, a finished download) |
| error | `on_error(std::exception_ptr)` | the sequence failed; no more values will come |
| unsubscription | none | the consumer stopped listening; the producer is cancelled |

Errors travel **downstream** through operators until something handles them. If nothing does, the
subscriber's `on_error` is called. If the subscriber did not provide one, the error is silently
ignored. Pass an `on_error` for any stream that can fail.

```cpp
auto s = download(url).subscribe(
    [](const Bytes& b) { save(b); },
    [](std::exception_ptr e) {
      try { std::rethrow_exception(e); }
      catch (const std::exception& ex) { show_error(ex.what()); }
    },
    [] { show_status("done"); });
```

### Where errors come from

- **Sources** call `on_error` (a failed request, `error<T>(...)`, `first()` on an empty stream).
- **Operators** create errors: `timeout` emits `pulse::timeout_error`; `first`/`last` emit
  `std::out_of_range` on empty input.
- **Your callbacks do not.** An exception thrown inside `map`, `filter` or `subscribe`'s `on_next`
  propagates up the producer's call stack (out of `on_next`, `emit`, `publish` or `set`). It is not
  converted into `on_error`. Treat callbacks like destructors: do not let exceptions escape. If a step
  can fail, model the failure as a value (`std::expected`, `std::optional`, a result struct) or catch
  it inside and turn it into an error stream in a `switch_map`.

```cpp
auto parsed = lines | pulse::switch_map([](const std::string& line) -> pulse::observable<Record> {
  try { return pulse::just(parse(line)); }
  catch (...) { return pulse::error<Record>(std::current_exception()); }
});
```

## Handling errors

### `catch_error(handler)`: switch to a fallback stream

`handler(std::exception_ptr)` returns an observable to continue with. Values emitted before the error
are kept.

```cpp
auto config = load_remote_config()
            | pulse::catch_error([](std::exception_ptr) { return load_cached_config(); });
```

The fallback can inspect the error and rethrow-by-returning an error stream:

```cpp
| pulse::catch_error([](std::exception_ptr e) -> pulse::observable<Data> {
    try { std::rethrow_exception(e); }
    catch (const pulse::timeout_error&) { return pulse::just(Data::empty()); }
    catch (...) { return pulse::error<Data>(e); }      // not ours: pass it on
  })
```

### `on_error_return(value)`: replace the error with a value

Emits `value` and completes.

```cpp
auto avatar = fetch_avatar(user) | pulse::on_error_return(Image::placeholder());
```

### `retry(k)`: try again

Resubscribes to the source up to `k` more times. The previous attempt is unsubscribed before the next
one starts. `retry(0)` forwards the first error.

```cpp
auto data = fetch(url) | pulse::retry(3);     // up to 4 attempts in total
```

`retry` resubscribes immediately. For a delay between attempts, retry a stream that starts with a
delay:

```cpp
auto attempt = [&] { return pulse::timer(500ms, pool) | pulse::switch_map([&](int) { return fetch(url); }); };
auto data = pulse::defer(attempt) | pulse::retry(5);
```

Only cold sources can be retried meaningfully, because each retry is a new subscription. Retrying a
`subject` that already errored immediately receives the same error again.

### `timeout(d)`: turn silence into an error

```cpp
auto reply = send_request(r)
           | pulse::timeout(3s)                       // pulse::timeout_error after 3 s of silence
           | pulse::retry(2)
           | pulse::on_error_return(Reply::failed());
```

### `finally_do(fn)`: cleanup on every ending

```cpp
busy = true;
auto s = (job() | pulse::finally_do([&] { busy = false; }))   // completion, error or unsubscribe
             .subscribe(on_result, on_error);
```

### `default_if_empty(value)`

Not an error handler, but it covers the "completed without result" case, which `first()` would
otherwise turn into an error.

## Completion in combinators

| Operator | Completes when |
|---|---|
| `map`, `filter`, `scan`, `tap`... | the source completes |
| `take(n)`, `take_while`, `take_until`, `first` | its condition is met (and it unsubscribes upstream) |
| `merge`, `combine_latest` | **all** sources have completed |
| `with_latest_from` | the main source completes |
| `zip` | one source has completed and its queue is drained |
| `switch_map` | the outer and the current inner have completed |
| `concat_map`, `flat_map` | the outer and all inner streams have completed |
| `debounce` | the source completes (after flushing the pending value) |
| `delay` | the delayed completion arrives |
| `share`, `ref_count` | the shared source completes; late subscribers receive the terminal event immediately |

Hot sources such as `property`, `signal`, `topic` and `interval` never complete on their own. Streams
built from them end when you unsubscribe, or when a `take*` operator ends them.

Next: [Sharing one source](11-sharing.md).
