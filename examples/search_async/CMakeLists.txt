add_executable(pulse_example_search_async
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)
target_link_libraries(pulse_example_search_async PRIVATE Pulse::pulse)
target_compile_features(pulse_example_search_async PRIVATE cxx_std_20)
