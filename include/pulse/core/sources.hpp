#pragma once
#include <exception>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulse/core/observable.hpp>

namespace pulse {

// just(a, b, c): emits the given values synchronously, then completes.
template <class T, class... Rest>
observable<std::decay_t<T>> just(T&& first, Rest&&... rest) {
  using V = std::decay_t<T>;
  auto values = std::make_shared<std::vector<V>>();
  values->reserve(1 + sizeof...(rest));
  values->emplace_back(std::forward<T>(first));
  (values->emplace_back(std::forward<Rest>(rest)), ...);
  return observable<V>::create([values](auto on_next, auto, auto on_done) {
    for (auto& v : *values)
      if (on_next) on_next(v);
    if (on_done) on_done();
    return subscription{};
  });
}

// from(container): emits every element, then completes.
template <class Container>
auto from(Container c) {
  using V = std::decay_t<decltype(*std::begin(c))>;
  auto data = std::make_shared<Container>(std::move(c));
  return observable<V>::create([data](auto on_next, auto, auto on_done) {
    for (auto& v : *data)
      if (on_next) on_next(v);
    if (on_done) on_done();
    return subscription{};
  });
}

template <class T>
observable<T> from(std::initializer_list<T> il) {
  return from(std::vector<T>(il));
}

// range(start, count): start, start+1, ..., start+count-1
template <class T = int>
observable<T> range(T start, std::size_t count) {
  return observable<T>::create([start, count](auto on_next, auto, auto on_done) {
    T v = start;
    for (std::size_t i = 0; i < count; ++i, ++v)
      if (on_next) on_next(v);
    if (on_done) on_done();
    return subscription{};
  });
}

template <class T>
observable<T> empty() {
  return observable<T>::create([](auto, auto, auto on_done) {
    if (on_done) on_done();
    return subscription{};
  });
}

template <class T>
observable<T> never() {
  return observable<T>::create([](auto, auto, auto) { return subscription{}; });
}

template <class T>
observable<T> error(std::exception_ptr e) {
  return observable<T>::create([e](auto, auto on_err, auto) {
    if (on_err) on_err(e);
    return subscription{};
  });
}

template <class T, class E>
observable<T> error(E ex) {
  return error<T>(std::make_exception_ptr(std::move(ex)));
}

// defer(factory): calls factory() on every subscription to get the real source.
template <class F>
auto defer(F factory) {
  using Obs = std::invoke_result_t<F&>;
  using T = typename Obs::value_type;
  return observable<T>::create([factory](auto on_next, auto on_err, auto on_done) mutable {
    return factory().subscribe(on_next, on_err, on_done);
  });
}

} // namespace pulse
