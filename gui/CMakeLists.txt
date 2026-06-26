# diffy-gui — the Slint git client. Reuses diffy_core for all diffing; libgit2
# only supplies repository/blob data.

# Slint: prefer an installed package, otherwise build it from source (matches the
# upstream slint-cpp-template). Building from source needs a Rust toolchain.
find_package(Slint QUIET)
if(NOT Slint_FOUND)
  message(STATUS "Slint not found locally — fetching and building from git (needs cargo).")
  include(FetchContent)
  FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG release/1
    SOURCE_SUBDIR api/cpp)
  FetchContent_MakeAvailable(Slint)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)

add_executable(diffy-gui
  src/main.cpp
  src/repo_model.cc
  src/diff_bridge.cc)

target_include_directories(diffy-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(diffy-gui PRIVATE diffy_core Slint::Slint PkgConfig::LIBGIT2)

# Compiles ui/app-window.slint and exposes the generated app-window.h.
slint_target_sources(diffy-gui ui/app-window.slint)

# On Windows, drop the Slint runtime DLL next to the binary.
if(WIN32)
  add_custom_command(TARGET diffy-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:diffy-gui> $<TARGET_FILE_DIR:diffy-gui>
    COMMAND_EXPAND_LISTS)
endif()
