# Minimal CMake wrapper for the vendored musl getopt (was Meson-only).
cmake_minimum_required(VERSION 3.21)
project(getopt LANGUAGES CXX)

add_library(getopt STATIC getopt.cc)
target_include_directories(getopt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(getopt PUBLIC cxx_std_20)
