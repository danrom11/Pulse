#pragma once
// Pulse - reactive programming and object interaction for C++20.
// Umbrella header: includes the whole library.

#include <pulse/version.hpp>

// core
#include <pulse/core/subscription.hpp>
#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/thread_pool.hpp>
#include <pulse/core/backpressure.hpp>
#include <pulse/core/observable.hpp>
#include <pulse/core/pipeline.hpp>
#include <pulse/core/sources.hpp>
#include <pulse/core/topic.hpp>
#include <pulse/core/topic_to_observable.hpp>
#include <pulse/core/subject.hpp>
#include <pulse/core/signal.hpp>
#include <pulse/core/property.hpp>

// operators
#include <pulse/ops/map.hpp>
#include <pulse/ops/filter.hpp>
#include <pulse/ops/tap.hpp>
#include <pulse/ops/skip.hpp>
#include <pulse/ops/take.hpp>
#include <pulse/ops/distinct.hpp>
#include <pulse/ops/start_with.hpp>
#include <pulse/ops/buffer.hpp>
#include <pulse/ops/window.hpp>
#include <pulse/ops/observe_on.hpp>
#include <pulse/ops/subscribe_on.hpp>
#include <pulse/ops/timer.hpp>
#include <pulse/ops/delay.hpp>
#include <pulse/ops/debounce.hpp>
#include <pulse/ops/throttle.hpp>
#include <pulse/ops/throttle_latest.hpp>
#include <pulse/ops/timeout.hpp>
#include <pulse/ops/retry.hpp>
#include <pulse/ops/catch_error.hpp>
#include <pulse/ops/switch_map.hpp>
#include <pulse/ops/concat_map.hpp>
#include <pulse/ops/flat_map.hpp>
#include <pulse/ops/merge.hpp>
#include <pulse/ops/combine_latest.hpp>
#include <pulse/ops/with_latest_from.hpp>
#include <pulse/ops/zip.hpp>
#include <pulse/ops/share.hpp>
#include <pulse/ops/publish.hpp>
