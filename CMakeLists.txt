cmake_minimum_required(VERSION 3.21)

project(Pulse VERSION 0.2.0 LANGUAGES CXX)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/pulse/version.hpp
  @ONLY
)

# -------------------------------
# Options (tests/examples default to ON only when Pulse is the top-level project)
# -------------------------------
option(PULSE_WITH_QT          "Enable Qt adapters (adapters/qt.hpp)" OFF)
option(PULSE_BUILD_TESTS      "Build tests"      ${PROJECT_IS_TOP_LEVEL})
option(PULSE_BUILD_EXAMPLES   "Build examples"   ${PROJECT_IS_TOP_LEVEL})
option(PULSE_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(PULSE_RUN_TESTS_ON_BUILD "Run the test suite as part of 'all'" OFF)
option(PULSE_INSTALL          "Generate install rules" ${PROJECT_IS_TOP_LEVEL})

# -------------------------------
# Library (header-only)
# -------------------------------
add_library(pulse INTERFACE)
add_library(Pulse::pulse ALIAS pulse)
add_library(Pulse ALIAS pulse) # backward compatibility

target_include_directories(pulse INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(pulse INTERFACE cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(pulse INTERFACE Threads::Threads)

target_compile_definitions(pulse INTERFACE
  $<$<BOOL:${PULSE_WITH_QT}>:PULSE_WITH_QT=1>
)

if(MSVC)
  # Correct __cplusplus value and standard-conforming preprocessor for consumers.
  target_compile_options(pulse INTERFACE /Zc:__cplusplus)
endif()

# --------------------------------
# Install / export
# --------------------------------
if(PULSE_INSTALL)
  include(CMakePackageConfigHelpers)
  include(GNUInstallDirs)

  set(PULSE_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Pulse")

  install(TARGETS pulse EXPORT PulseTargets)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/pulse/version.hpp
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pulse)

  install(EXPORT PulseTargets
    NAMESPACE Pulse::
    DESTINATION ${PULSE_CMAKE_INSTALL_DIR}
  )
  export(EXPORT PulseTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/PulseTargets.cmake"
    NAMESPACE Pulse::
  )

  set(PACKAGE_INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_INCLUDEDIR}")
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PulseConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PulseConfig.cmake"
    INSTALL_DESTINATION "${PULSE_CMAKE_INSTALL_DIR}"
    PATH_VARS PACKAGE_INCLUDE_INSTALL_DIR
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PulseConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PulseConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PulseConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_warnings.cmake"
    DESTINATION "${PULSE_CMAKE_INSTALL_DIR}"
  )
endif()

# --------------------------------
# Tests
# --------------------------------
if(PULSE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)

  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    USES_TERMINAL
    COMMENT "Running Pulse tests"
  )
  add_dependencies(check ${PULSE_TEST_TARGETS})
  if(PULSE_RUN_TESTS_ON_BUILD)
    set_target_properties(check PROPERTIES EXCLUDE_FROM_ALL OFF)
  endif()
endif()

if(PULSE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(PULSE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

message(STATUS "Pulse ${PROJECT_VERSION} configured.")
