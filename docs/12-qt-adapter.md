# 12. Qt adapter

`#include <pulse/adapters/qt.hpp>` integrates Pulse with Qt 5.10+ and Qt 6. The adapter is a single
header and is **not** included by `pulse/pulse.hpp`, so Pulse itself never depends on Qt. Link your
target against Qt as usual. `-DPULSE_WITH_QT=ON` only enables the Qt examples of this repository.

Everything is in `namespace pulse::qt`.

## `qt_executor`: run on a QObject's thread

```cpp
pulse::qt::qt_executor ui;                     // targets QCoreApplication::instance() (the main thread)
pulse::qt::qt_executor on_widget{my_widget};   // runs in my_widget's thread (via a queued invocation)
```

`post` uses `QMetaObject::invokeMethod(target, fn, Qt::QueuedConnection)`. Every Pulse feature that
takes an executor works with it:

```cpp
auto s = (worker_results | pulse::observe_on(ui)).subscribe([label](const QString& s) { label->setText(s); });
auto d = text_changes | pulse::debounce(300ms, ui);
```

`pulse::qt::observe_on(QObject*)` is shorthand that creates a shared executor for that object:

```cpp
auto s = (stream | pulse::qt::observe_on(window)).subscribe(update_ui);
```

## Qt signals as observables

```cpp
// A signal with several arguments -> observable<std::tuple<Args...>>
auto moves = pulse::qt::from_signal(view, &MyView::mouseMoved);        // tuple<int, int>

// A signal with one argument -> observable<T>
auto text = pulse::qt::from_signal1(edit, &QLineEdit::textChanged);    // observable<QString>

auto s = (text | pulse::debounce(300ms, ui)
               | pulse::distinct_until_changed()
               | pulse::switch_map(search))
             .subscribe([list](const Results& r) { list->show(r); });
```

The connection is queued, is removed on unsubscribe, and completes the stream when the sender is
destroyed (it watches `QObject::destroyed`).

## Qt timers

```cpp
auto ticks = pulse::qt::qt_interval(100ms, owner);   // observable<int>, driven by a QTimer
auto once  = pulse::qt::qt_timer(2s, owner);
```

These use `QTimer` on the owner's thread instead of Pulse's timer thread. They are handy when
everything should stay inside Qt's event loop.

## Typical Qt pattern: search-as-you-type

```cpp
class SearchWidget : public QWidget {
public:
  SearchWidget() {
    auto query = pulse::qt::from_signal1(edit_, &QLineEdit::textChanged)
               | pulse::map([](const QString& s) { return s.trimmed(); })
               | pulse::debounce(250ms, ui_)
               | pulse::distinct_until_changed();

    sub_ = (query | pulse::switch_map([this](const QString& q) { return api_.search(q); })
                  | pulse::observe_on(ui_))
               .subscribe([this](const QStringList& r) { model_.setStringList(r); });
  }

private:
  QLineEdit* edit_ = new QLineEdit(this);
  QStringListModel model_;
  Api api_;
  pulse::qt::qt_executor ui_{this};
  pulse::subscription sub_;          // last: cancelled first when the widget is destroyed
};
```

See `examples/qt_search_demo` for a complete program.

Next: [Patterns and pitfalls](13-patterns-and-pitfalls.md).
