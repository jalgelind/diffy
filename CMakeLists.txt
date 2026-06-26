cmake_minimum_required(VERSION 3.21)

# Several vendored subprojects (crc32c, GSL, fmt, platform_folders) still pin
# cmake_minimum_required below 3.5; CMake >= 4 refuses that unless we raise the
# policy floor. Must be set before those subdirectories are added.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Compat floor for old subprojects" FORCE)

project(diffy LANGUAGES CXX VERSION 24.07)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DIFFY_BUILD_CLI   "Build the diffy command-line tool"   ON)
option(DIFFY_BUILD_GUI   "Build the diffy-gui git client"      OFF)
option(DIFFY_BUILD_TESTS "Build the diffy test suite"          ON)

if(WIN32)
  add_compile_definitions(DIFFY_PLATFORM_WINDOWS)
else()
  add_compile_definitions(DIFFY_PLATFORM_POSIX)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(DIFFY_DEBUG=1)
endif()

# Short VCS hash baked into the CLI's --version output.
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE DIFFY_BUILD_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT DIFFY_BUILD_HASH)
  set(DIFFY_BUILD_HASH "unknown")
endif()

# --- vendored third-party dependencies -------------------------------------
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/fmt EXCLUDE_FROM_ALL)

set(GSL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/GSL EXCLUDE_FROM_ALL)

set(CRC32C_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CRC32C_USE_GLOG OFF CACHE BOOL "" FORCE)
set(CRC32C_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/crc32c EXCLUDE_FROM_ALL)

add_subdirectory(subprojects/platform_folders EXCLUDE_FROM_ALL)
add_subdirectory(subprojects/getopt)
add_subdirectory(subprojects/config_parser)

# --- our libraries / applications ------------------------------------------
add_subdirectory(libdiffy)

if(DIFFY_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(DIFFY_BUILD_GUI)
  add_subdirectory(gui)
endif()

if(DIFFY_BUILD_TESTS)
  enable_testing()
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory(subprojects/doctest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
