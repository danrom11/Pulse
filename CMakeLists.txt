cmake_minimum_required(VERSION 3.21)

project(Pulse VERSION 0.1.0 LANGUAGES CXX)

# -------------------------------
# Options
# -------------------------------
option(PULSE_WITH_QT "Enable Qt adapters (adapters/qt.hpp)" OFF)
# option(PULSE_TRACE   "Enable tracing hooks" OFF) TODO: Implement in the future
option(PULSE_BUILD_TESTS "Build tests" ON)
option(PULSE_BUILD_EXAMPLES "Build examples" ON)
option(PULSE_BUILD_BENCHMARKS "Build benchmarks" OFF)

# -------------------------------
# Library (header-only)
# -------------------------------
# Canonical target name: Pulse::pulse
add_library(pulse INTERFACE)
add_library(Pulse::pulse ALIAS pulse)
add_library(Pulse ALIAS pulse)

target_include_directories(pulse
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(pulse INTERFACE cxx_std_20)

# Public API definitions
target_compile_definitions(pulse
  INTERFACE
    $<$<BOOL:${PULSE_TRACE}>:PULSE_TRACE=1>
    $<$<NOT:$<BOOL:${PULSE_TRACE}>>:PULSE_TRACE=0>
    $<$<BOOL:${PULSE_WITH_QT}>:PULSE_WITH_QT=1>
    $<$<NOT:$<BOOL:${PULSE_WITH_QT}>>:PULSE_WITH_QT=0>
)

# --------------------------------
# Export/Install Package
# --------------------------------
include(CMakePackageConfigHelpers)

# Where to put the package's cmake configs
set(PULSE_CMAKE_INSTALL_DIR "lib/cmake/Pulse")

install(TARGETS pulse
  EXPORT PulseTargets
  INCLUDES DESTINATION include
)
install(DIRECTORY include/ DESTINATION include)

# Export targets (for the installed package)
install(EXPORT PulseTargets
  NAMESPACE Pulse::
  DESTINATION ${PULSE_CMAKE_INSTALL_DIR}
)

export(EXPORT PulseTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/PulseTargets.cmake"
  NAMESPACE Pulse::
)

set(PULSE_CONFIG_IN   "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PulseConfig.cmake.in")
set(PULSE_WARNINGS_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_warnings.cmake")

set(PACKAGE_CMAKE_INSTALL_DIR "${PULSE_CMAKE_INSTALL_DIR}")
set(PACKAGE_INCLUDE_INSTALL_DIR "include")

configure_package_config_file(
  "${PULSE_CONFIG_IN}"
  "${CMAKE_CURRENT_BINARY_DIR}/PulseConfig.cmake"
  INSTALL_DESTINATION "${PULSE_CMAKE_INSTALL_DIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PulseConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PulseConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PulseConfigVersion.cmake"
  "${PULSE_WARNINGS_IN}"
  DESTINATION "${PULSE_CMAKE_INSTALL_DIR}"
)

# --------------------------------
# Examples and Tests
# --------------------------------
if (PULSE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if (PULSE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)

  add_custom_target(run_tests ALL
    COMMAND ${CMAKE_CTEST_COMMAND}
            --output-on-failure
            -C $<CONFIG>
            --test-dir "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    USES_TERMINAL
    COMMENT "Running Pulse tests via CTest"
    DEPENDS ${PULSE_TEST_TARGETS}
  )
endif()

if (PULSE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# --------------------------------
# Status Message
# --------------------------------
message(STATUS "Pulse ${PROJECT_VERSION} configured.")
