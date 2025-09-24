add_executable(pulse_example_distinct_timeout
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)
target_link_libraries(pulse_example_distinct_timeout PRIVATE Pulse::pulse)
target_compile_features(pulse_example_distinct_timeout PRIVATE cxx_std_20)

