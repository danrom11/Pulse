# compiler_warnings.cmake
# Helper utilities to configure consistent warning levels across compilers.
#
# Usage:
#   include(compiler_warnings)   # or pulse_include_warnings() if using PulseConfig
#   pulse_enable_warnings(<target> [AS_ERROR] [NO_PEDANTIC])
#   pulse_disable_warnings(<target>)
#   pulse_create_warnings_interface(TARGET <name> [AS_ERROR] [NO_PEDANTIC])
#
# Notes:
# - Works with MSVC, GCC, Clang (incl. AppleClang).
# - Adds warnings for C and C++ sources attached to the target.
# - Generator expressions keep flags per-compiler.
#
# Example:
#   add_executable(app main.cpp)
#   pulse_enable_warnings(app AS_ERROR)        # strict warnings, treat as errors
#
#   # Or share warnings via interface target:
#   pulse_create_warnings_interface(TARGET Pulse::Warnings AS_ERROR)
#   target_link_libraries(app PRIVATE Pulse::Warnings)

include(CMakeDependentOption)

function(_pulse_collect_warning_flags out as_error no_pedantic)
  # Common flags
  set(_gcc_like_base
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion
    -Wformat=2 -Wundef -Wcast-qual -Wcast-align -Wnon-virtual-dtor
    -Woverloaded-virtual -Wold-style-cast -Wimplicit-fallthrough
    -Wduplicated-branches -Wduplicated-cond -Wnull-dereference
    -Wno-psabi
  )
  if (NOT ${no_pedantic})
    list(APPEND _gcc_like_base -Wpedantic)
  endif()

  set(_clang_extra
    -Wno-c++98-compat -Wno-c++98-compat-pedantic
    -Wno-missing-prototypes
  )

  set(_msvc_base
    /W4 /permissive- /Zc:__cplusplus
    /w44265  # switch-enum
    /w44062  # enumerator 'X' in a switch of enum 'Y' is not handled
    /w44061  # enumerator 'X' in a switch of enum 'Y' is not explicitly handled by a 'case' label
  )
  if (${as_error})
    set(_gcc_like_error -Werror)
    set(_msvc_error /WX)
  else()
    set(_gcc_like_error "")
    set(_msvc_error "")
  endif()

  # Compose with generator expressions so it is safe on all generators.
  set(${out}
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:${_gcc_like_base} ${_gcc_like_error}>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:${_gcc_like_base} ${_gcc_like_error}>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:${_clang_extra}>
    $<$<C_COMPILER_ID:Clang,AppleClang>:${_clang_extra}>
    $<$<CXX_COMPILER_ID:MSVC>:${_msvc_base} ${_msvc_error}>
    $<$<C_COMPILER_ID:MSVC>:${_msvc_base} ${_msvc_error}>
  PARENT_SCOPE)
endfunction()

function(pulse_enable_warnings target)
  if (NOT TARGET ${target})
    message(FATAL_ERROR "pulse_enable_warnings: target '${target}' does not exist")
  endif()

  set(options AS_ERROR NO_PEDANTIC)
  cmake_parse_arguments(PW "${options}" "" "" ${ARGN})

  _pulse_collect_warning_flags(_flags ${PW_AS_ERROR} ${PW_NO_PEDANTIC})
  target_compile_options(${target} PRIVATE ${_flags})
endfunction()

# Create reusable interface target with warnings you can link against
function(pulse_create_warnings_interface)
  set(oneValueArgs TARGET)
  set(options AS_ERROR NO_PEDANTIC)
  cmake_parse_arguments(PWI "${options}" "${oneValueArgs}" "" ${ARGN})

  if (NOT PWI_TARGET)
    message(FATAL_ERROR "pulse_create_warnings_interface: pass TARGET <name>")
  endif()

  add_library(${PWI_TARGET} INTERFACE)
  _pulse_collect_warning_flags(_flags ${PWI_AS_ERROR} ${PWI_NO_PEDANTIC})
  target_compile_options(${PWI_TARGET} INTERFACE ${_flags})
endfunction()

# Disable warnings for a given target (useful for third-party code)
function(pulse_disable_warnings target)
  if (NOT TARGET ${target})
    message(FATAL_ERROR "pulse_disable_warnings: target '${target}' does not exist")
  endif()

  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/w>
    $<$<C_COMPILER_ID:MSVC>:/w>
  )
endfunction()
