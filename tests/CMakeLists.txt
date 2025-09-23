# Tests for Pulse (header-only). Enable from top-level CMakeLists with:
# include(CTest); enable_testing(); add_subdirectory(tests)

cmake_minimum_required(VERSION 3.21)

set(PULSE_TARGET Pulse::pulse CACHE STRING "Pulse library target to link with tests")

find_package(Threads REQUIRED)

set(PULSE_TEST_TARGETS "" CACHE INTERNAL "Pulse test targets")

function(pulse_add_test NAME)
  # usage: pulse_add_test(pulse_basic_topic_tests basic_topic_tests.cpp)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs)
  cmake_parse_arguments(PAT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(_srcs ${ARGN})

  add_executable(${NAME} ${_srcs})
  target_compile_features(${NAME} PRIVATE cxx_std_20)
  target_link_libraries(${NAME} PRIVATE ${PULSE_TARGET} Threads::Threads)

  target_include_directories(${NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )

  add_test(NAME ${NAME} COMMAND $<TARGET_FILE:${NAME}>)

  set(PULSE_TEST_TARGETS ${PULSE_TEST_TARGETS} ${NAME} CACHE INTERNAL "Pulse test targets")
endfunction()

# --- Test List ---
pulse_add_test(pulse_basic_topic_tests                basic_topic_tests.cpp)
pulse_add_test(pulse_observable_ops_tests             observable_ops_tests.cpp)
pulse_add_test(pulse_debounce_tests                   debounce_tests.cpp)
pulse_add_test(pulse_publish_refcount_tests           publish_refcount_tests.cpp)
pulse_add_test(pulse_backpressure_tests               backpressure_tests.cpp)
pulse_add_test(pulse_combine_latest_tests             combine_latest_tests.cpp)
pulse_add_test(pulse_distinct_take_tests              distinct_and_take_tests.cpp)
pulse_add_test(pulse_switch_map_cancel_tests          switch_map_cancel_tests.cpp)
pulse_add_test(pulse_timer_interval_tests             timer_interval_tests.cpp)
pulse_add_test(pulse_refcount_no_grace_tests          refcount_no_grace_tests.cpp)
pulse_add_test(pulse_zip_interval_tests               zip_interval_tests.cpp)
pulse_add_test(pulse_share_grace_reuse_tests          share_grace_reuse_tests.cpp)
pulse_add_test(pulse_take_unsubscribe_tests           take_unsubscribe_upstream_tests.cpp)
pulse_add_test(pulse_timeout_success_failure_tests    timeout_success_failure_tests.cpp)
pulse_add_test(pulse_observe_on_threading_tests       observe_on_threading_tests.cpp)
pulse_add_test(pulse_error_propagation_tests          error_propagation_tests.cpp)
pulse_add_test(pulse_throttle_tests                   throttle_tests.cpp)
pulse_add_test(pulse_throttle_latest_tests            throttle_latest_tests.cpp)
pulse_add_test(pulse_buffer_tests                     buffer_tests.cpp)
pulse_add_test(pulse_concat_map_tests                 concat_map_tests.cpp)
pulse_add_test(pulse_subscribe_on_tests               subscribe_on_tests.cpp)
pulse_add_test(pulse_merge_tests                      merge_tests.cpp)
pulse_add_test(pulse_window_tests                     window_tests.cpp)
