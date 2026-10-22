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

# Hardware loopback: hub + client on this computer with the real microphone and headset.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
add_executable(labaudio_loopback labaudio_loopback.cpp
	../LabAudioHub.cpp ../LabAudioHub.h ../LabAudioClient.cpp ../LabAudioClient.h ../LabAudioEngine.cpp ../LabAudioEngine.h)
target_include_directories(labaudio_loopback BEFORE PRIVATE shim)
target_link_libraries(labaudio_loopback PRIVATE Qt6::Widgets Qt6::Network Qt6::Multimedia PkgConfig::OPUS)

# Group work routing with simulated students (no audio devices needed while no group is in focus).
add_executable(labaudio_grouptest labaudio_grouptest.cpp
	../LabAudioHub.cpp ../LabAudioHub.h ../LabAudioGroupWork.cpp ../LabAudioGroupWork.h ../LabAudioEngine.cpp ../LabAudioEngine.h)
target_include_directories(labaudio_grouptest BEFORE PRIVATE shim)
target_link_libraries(labaudio_grouptest PRIVATE Qt6::Widgets Qt6::Network Qt6::Multimedia PkgConfig::OPUS)
