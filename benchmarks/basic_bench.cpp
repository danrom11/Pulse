#include <benchmark/benchmark.h>
#include <pulse/pulse.hpp>
#include <chrono>

using namespace pulse;
using namespace std::chrono_literals;

static void BM_filter_even(benchmark::State& st) {
  inline_executor ui;
  topic<int> t;
  auto o = as_observable(t, ui);
  volatile int sink = 0;

  auto sub = (o | filter([](int x){ return (x & 1) == 0; }))
      .subscribe([&](int x){
        sink = x;
        benchmark::DoNotOptimize(sink);
      });

  for (auto _ : st) {
    for (int i = 0; i < st.range(0); ++i) {
      benchmark::DoNotOptimize(i);
      t.publish(i);
    }
  }
}
BENCHMARK(BM_filter_even)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_filter_even_inline(benchmark::State& state) {
  inline_executor ui;
  topic<int> t;
  auto o = as_observable(t, ui);
  volatile int sink = 0;

  auto sub = (o | filter([](int x){ return x % 2 == 0; }))
      .subscribe([&](int x){
        sink = x;
        benchmark::DoNotOptimize(sink);
      });

  for (auto _ : state) {
    for (int i = 0; i < state.range(0); ++i) {
      benchmark::DoNotOptimize(i);
      t.publish(i);
    }
  }
}
BENCHMARK(BM_filter_even_inline)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_filter_even_pool(benchmark::State& state) {
  thread_pool pool{2};
  topic<int> t;
  auto o = as_observable(t, pool);
  volatile int sink = 0;

  auto sub = (o | filter([](int x){ return x % 2 == 0; }))
      .subscribe([&](int x){
        sink = x;
        benchmark::DoNotOptimize(sink);
      });

  for (auto _ : state) {
    for (int i = 0; i < state.range(0); ++i) {
      benchmark::DoNotOptimize(i);
      t.publish(i);
    }
  }
}
BENCHMARK(BM_filter_even_pool)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_map_chain(benchmark::State& state) {
  inline_executor ui;
  topic<int> t;
  auto o = as_observable(t, ui)
         | map([](int x){ return x+1; })
         | map([](int x){ return x*2; })
         | map([](int x){ return x-3; });
  volatile int sink = 0;

  auto sub = o.subscribe([&](int v){
    sink = v;
    benchmark::DoNotOptimize(sink);
  });

  for (auto _ : state) {
    for (int i = 0; i < state.range(0); ++i) {
      benchmark::DoNotOptimize(i);
      t.publish(i);
    }
  }
}
BENCHMARK(BM_map_chain)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_throttle_latest(benchmark::State& state) {
  thread_pool pool{1};
  topic<int> t;
  auto o = as_observable(t, pool) | throttle_latest(1ms, pool);
  volatile int sink = 0;

  auto sub = o.subscribe([&](int v){
    sink = v;
    benchmark::DoNotOptimize(sink);
  });

  for (auto _ : state) {
    for (int i = 0; i < state.range(0); ++i) {
      benchmark::DoNotOptimize(i);
      t.publish(i);
    }
  }
}
BENCHMARK(BM_throttle_latest)->Arg(100)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();
