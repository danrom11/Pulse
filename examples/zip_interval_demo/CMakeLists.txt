add_executable(pulse_example_zip_interval
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)
target_link_libraries(pulse_example_zip_interval PRIVATE Pulse)
target_compile_features(pulse_example_zip_interval PRIVATE cxx_std_20)

