# Standalone self-test for the Lab Audio engine (no Veyon, no audio hardware needed).
# Build: cmake -S plugins/labaudio/selftest -B build-selftest -G Ninja && ninja -C build-selftest
cmake_minimum_required(VERSION 3.18)
project(labaudio_selftest CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
add_executable(labaudio_selftest labaudio_selftest.cpp ../LabAudioEngine.cpp ../LabAudioEngine.h)
target_link_libraries(labaudio_selftest PRIVATE Qt6::Core Qt6::Multimedia PkgConfig::OPUS)
