#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QMetaObject>

#include <chrono>
#include <tuple>
#include <type_traits>
#include <utility>
#include <memory>
#include <functional>

#include <pulse/core/scheduler.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulse/core/pipeline.hpp>
#include <pulse/ops/map.hpp>
#include <pulse/ops/observe_on.hpp>

namespace pulse {
namespace qt {

// ====================================================================================
// 1) qt_executor - “bridge” to the Qt stream via QMetaObject::invokeMethod
// ====================================================================================
class qt_executor : public executor {
public:
  explicit qt_executor(QObject* target = QCoreApplication::instance())
  : target_(target ? target : QCoreApplication::instance()) {}

  void post(std::function<void()> f) override {
    QObject* tgt = target_;
    if (!tgt) { f(); return; }

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QMetaObject::invokeMethod(
      tgt,
      [fn = std::move(f)]() mutable { fn(); },
      Qt::QueuedConnection
    );
#else
    QTimer::singleShot(0, tgt, [fn = std::move(f)]() mutable { fn(); });
#endif
  }

  QObject* target() const { return target_; }

private:
  QObject* target_ = nullptr;
};

// ====================================================================================
// 2) QTimer-based Timers: qt_interval / qt_timer
// ====================================================================================
template <class Rep, class Period>
inline observable<int> qt_interval(std::chrono::duration<Rep, Period> period,
                                   QObject* target = QCoreApplication::instance())
{
  using namespace std::chrono;
  const auto ms = duration_cast<milliseconds>(period).count();

  return observable<int>::create([ms, target](auto on_next, auto, auto on_completed) {
    QPointer<QObject> guard(target ? target : QCoreApplication::instance());

    auto timer = std::make_shared<QTimer>();
    timer->setInterval(static_cast<int>(ms));
    timer->setSingleShot(false);

    auto tick = std::make_shared<int>(0);

    auto unsub = [timer]{
      if (timer && timer->isActive()) timer->stop();
      if (timer && timer->thread() == QThread::currentThread()) {
        timer->deleteLater();
      } else if (timer) {
        QMetaObject::invokeMethod(timer.get(), "deleteLater", Qt::QueuedConnection);
      }
    };

    QObject::connect(timer.get(), &QTimer::timeout, timer.get(), [on_next, tick]{
      if (on_next) on_next((*tick)++);
    });

    if (guard) {
      QObject::connect(guard, &QObject::destroyed, timer.get(), [on_completed, unsub]{
        unsub();
        if (on_completed) on_completed();
      });
    }

    if (guard && guard->thread() && guard->thread() != QThread::currentThread()) {
      timer->moveToThread(guard->thread());
      QMetaObject::invokeMethod(timer.get(), "start", Qt::QueuedConnection);
    } else {
      timer->start();
    }

    return subscription(unsub);
  });
}

template <class Rep, class Period>
inline observable<int> qt_timer(std::chrono::duration<Rep, Period> delay,
                                QObject* target = QCoreApplication::instance())
{
  using namespace std::chrono;
  const auto ms = duration_cast<milliseconds>(delay).count();

  return observable<int>::create([ms, target](auto on_next, auto, auto on_completed) {
    QPointer<QObject> guard(target ? target : QCoreApplication::instance());

    auto timer = std::make_shared<QTimer>();
    timer->setInterval(static_cast<int>(ms));
    timer->setSingleShot(true);

    auto unsub = [timer]{
      if (timer && timer->isActive()) timer->stop();
      if (timer && timer->thread() == QThread::currentThread()) {
        timer->deleteLater();
      } else if (timer) {
        QMetaObject::invokeMethod(timer.get(), "deleteLater", Qt::QueuedConnection);
      }
    };

    QObject::connect(timer.get(), &QTimer::timeout, timer.get(), [on_next, on_completed, unsub]{
      if (on_next) on_next(0);
      unsub();
      if (on_completed) on_completed();
    });

    if (guard) {
      QObject::connect(guard, &QObject::destroyed, timer.get(), [on_completed, unsub]{
        unsub();
        if (on_completed) on_completed();
      });
    }

    if (guard && guard->thread() && guard->thread() != QThread::currentThread()) {
      timer->moveToThread(guard->thread());
      QMetaObject::invokeMethod(timer.get(), "start", Qt::QueuedConnection);
    } else {
      timer->start();
    }

    return subscription(unsub);
  });
}

// ====================================================================================
// 3) from_signal / from_signal1 - Qt signal -> observable
// ====================================================================================
template <typename Sender, typename... Args>
inline observable<std::tuple<std::decay_t<Args>...>>
from_signal(Sender* sender, void (Sender::*signal)(Args...)) {
  return observable<std::tuple<std::decay_t<Args>...>>::create(
    [sender, signal](auto on_next, auto, auto on_completed)
  {
    QPointer<Sender> guard(sender);
    auto connection = std::make_shared<QMetaObject::Connection>();

    auto unsub = [connection, guard, on_completed]{
      if (connection && *connection) QObject::disconnect(*connection);
      if (on_completed) on_completed();
    };

    if (!guard) { unsub(); return subscription{}; }

    *connection = QObject::connect(
      guard,
      signal,
      guard,
      [guard, on_next](Args... args){
        if (!guard) return;
        if (on_next) on_next(std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...));
      },
      Qt::QueuedConnection
    );

    QObject::connect(guard, &QObject::destroyed, guard, [unsub]{ unsub(); });
    return subscription(unsub);
  });
}

// Signal with one argument -> observable<T>
template <typename Sender, typename T>
inline observable<std::decay_t<T>>
from_signal1(Sender* sender, void (Sender::*signal)(T)) {
  using U = std::decay_t<T>;
  return from_signal(sender, signal) | map([](auto&& tup) -> U {
    return std::get<0>(tup);
  });
}

// ====================================================================================
// 4) observe_on(QObject*) — sugar: use the observe_on(shared_ptr<executor>) overload
// ====================================================================================
inline auto observe_on(QObject* target) {
  return [target](const auto& src){
    auto exec = std::make_shared<qt_executor>(target);
    return src | ::pulse::observe_on(exec);
  };
}

} // namespace qt
} // namespace pulse
