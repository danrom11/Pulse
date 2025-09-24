add_executable(pulse_example_combine_latest
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)
target_link_libraries(pulse_example_combine_latest PRIVATE Pulse::pulse)
target_compile_features(pulse_example_combine_latest PRIVATE cxx_std_20)

