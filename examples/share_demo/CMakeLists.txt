add_executable(pulse_example_share_demo
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)
target_link_libraries(pulse_example_share_demo PRIVATE Pulse::pulse)
target_compile_features(pulse_example_share_demo PRIVATE cxx_std_20)
