cmake_minimum_required(VERSION 3.21)

set(CMAKE_AUTOMOC ON)

if (NOT DEFINED PULSE_QT_MAJOR)
  set(PULSE_QT_MAJOR 6)
endif()

if (PULSE_QT_MAJOR EQUAL 6)
  find_package(Qt6 6.2 COMPONENTS Core Widgets REQUIRED)
  set(PULSE_QT_CORE    Qt6::Core)
  set(PULSE_QT_WIDGETS Qt6::Widgets)
else()
  find_package(Qt5 5.12 COMPONENTS Core Widgets REQUIRED)
  set(PULSE_QT_CORE    Qt5::Core)
  set(PULSE_QT_WIDGETS Qt5::Widgets)
endif()

add_executable(pulse_example_qt_search
  main.cpp
)

target_link_libraries(pulse_example_qt_search
  PRIVATE
    Pulse
    ${PULSE_QT_CORE}
    ${PULSE_QT_WIDGETS}
)

target_compile_features(pulse_example_qt_search PRIVATE cxx_std_20)

target_include_directories(pulse_example_qt_search PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
