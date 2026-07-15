cmake_minimum_required(VERSION 3.21)

# Several vendored subprojects (crc32c, GSL, fmt, platform_folders) still pin
# cmake_minimum_required below 3.5; CMake >= 4 refuses that unless we raise the
# policy floor. Must be set before those subdirectories are added.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Compat floor for old subprojects" FORCE)

project(diffy LANGUAGES CXX VERSION 26.07.2)

# Absolute path to this (the diffy) source root. Prefer this over CMAKE_SOURCE_DIR
# for locating our own subprojects/: when diffy is embedded as a subdirectory
# (e.g. a frontend that vendors libdiffy as a submodule), CMAKE_SOURCE_DIR is
# the *superproject*, whereas this always points at diffy itself.
set(DIFFY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DIFFY_BUILD_CLI   "Build the diffy command-line tool"   ON)
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
  WORKING_DIRECTORY ${DIFFY_ROOT_DIR}
  OUTPUT_VARIABLE DIFFY_BUILD_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT DIFFY_BUILD_HASH)
  set(DIFFY_BUILD_HASH "unknown")
endif()

# --- vendored third-party dependencies -------------------------------------
# Build these vendored deps as STATIC regardless of the toolchain default. A vcpkg
# toolchain with a dynamic triplet (e.g. x64-windows) sets BUILD_SHARED_LIBS=ON,
# which makes fmt build as fmt.dll and leaves the CLI unable to start without it
# next to the exe. Scope the override to just these deps and restore it afterward
# so a consumer that embeds this tree as a subdirectory keeps the toolchain default.
set(_diffy_prev_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)

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

# Restore the toolchain default now that the forced-static vendored deps are
# configured (only those deps are forced static).
set(BUILD_SHARED_LIBS "${_diffy_prev_shared}")

# --- our libraries / applications ------------------------------------------
if(DIFFY_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(libdiffy)

if(DIFFY_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(DIFFY_BUILD_TESTS)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory(subprojects/doctest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
