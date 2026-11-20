# Pulse documentation

Pulse is a header-only C++20 library for two closely related jobs:

1. **Object interaction**: letting parts of a program talk to each other without knowing about each other.
   Use `signal`, `property` and `computed` for this.
2. **Reactive streams**: describing *how values flow and change over time* (debounce a search box,
   merge two event sources, retry a request) as a pipeline instead of hand-written state machines.
   Use `observable` and its operators for this.

Both halves share one lifetime model, `subscription`, and one threading model, `executor`, so they
compose freely. Any signal or property can become a stream, and any stream can drive a property.

## Reading order

If you are new to Pulse, read the guide chapters in order. Each one builds on the previous.

| # | Chapter | You will learn |
|---|---|---|
| 1 | [Introduction](01-introduction.md) | The mental model, and which tool to pick for which problem |
| 2 | [Getting started](02-getting-started.md) | Adding Pulse to a CMake project, a first program |
| 3 | [Observables](03-observables.md) | Streams, subscribing, sources, the completion and error contract, pipelines |
| 4 | [Subscriptions and lifetime](04-subscriptions.md) | RAII subscriptions, bags, and keeping objects and streams in sync |
| 5 | [Signals, properties, computed](05-signals-and-properties.md) | Qt-style object interaction, done with plain C++ |
| 6 | [Hot sources: subject, behavior_subject, topic](06-hot-sources.md) | Pushing values into streams; the event bus |
| 7 | [Operators reference](07-operators.md) | Every operator with semantics, signatures and examples |
| 8 | [Threads and time](08-threading-and-time.md) | Executors, `observe_on` / `subscribe_on`, timers, the timer service |
| 9 | [Backpressure](09-backpressure.md) | What to do when producers are faster than consumers |
| 10 | [Errors and completion](10-errors.md) | `catch_error`, `retry`, `timeout` and friends |
| 11 | [Sharing one source](11-sharing.md) | `share`, `publish` / `connect`, `ref_count` with a grace period |
| 12 | [Qt adapter](12-qt-adapter.md) | Using Pulse inside a Qt application |
| 13 | [Patterns and pitfalls](13-patterns-and-pitfalls.md) | Recipes for real code and the mistakes everybody makes once |
| 14 | [Cheat sheet](14-cheatsheet.md) | One page with everything |

## Conventions used in these docs

- Code assumes `#include <pulse/pulse.hpp>` and, for brevity, `using namespace pulse;`
  unless it shows `pulse::` explicitly.
- `using namespace std::chrono_literals;` is assumed wherever you see `250ms`.
- Marble diagrams show time flowing left to right:

  ```
  source:  --1--2--3--|        values 1, 2, 3, then completion ( | )
  map x10: --10-20-30-|
  error:   --1--X              X = error
  ```

## Related

- [PulseUI documentation](https://github.com/danrom11/PulseUI/tree/main/docs) is the reactive UI library built on Pulse.
- The `examples/` folder of this repository has runnable programs for most chapters.
