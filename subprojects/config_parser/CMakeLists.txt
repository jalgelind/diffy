# CMake wrapper for the vendored config_parser (was Meson-only). Exposes the
# `config_parser` static lib plus its src/ include root for <config_parser/...>.
cmake_minimum_required(VERSION 3.21)
project(config_parser LANGUAGES CXX)

add_library(config_parser STATIC
  src/config_parser/config_parser.cc
  src/config_parser/config_parser_utils.cc
  src/config_parser/config_serializer.cc
  src/config_parser/config_tokenizer.cc)

target_include_directories(config_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(config_parser PUBLIC cxx_std_20)
target_link_libraries(config_parser PUBLIC fmt::fmt)

if(WIN32)
  target_compile_definitions(config_parser PUBLIC CONFIG_PLATFORM_WINDOWS)
else()
  target_compile_definitions(config_parser PUBLIC CONFIG_PLATFORM_POSIX)
endif()
