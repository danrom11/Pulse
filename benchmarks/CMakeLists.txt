cmake_minimum_required(VERSION 3.21)
project(PulseBench LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(benchmark CONFIG REQUIRED)

add_executable(pulse_bench basic_bench.cpp)
target_link_libraries(pulse_bench PRIVATE Pulse::pulse benchmark::benchmark Threads::Threads)
target_compile_features(pulse_bench PRIVATE cxx_std_20)
