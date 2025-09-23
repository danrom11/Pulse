add_executable(pulse_example_minimal
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

target_link_libraries(pulse_example_minimal
  PRIVATE
    Pulse
)

target_compile_features(pulse_example_minimal PRIVATE cxx_std_20)
