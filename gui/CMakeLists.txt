# diffy-gui — the Slint git client. Reuses diffy_core for all diffing; libgit2
# only supplies repository/blob data.

# Render the built-in widgets with Slint's dark style so checkboxes, combo
# boxes, buttons and line edits stay legible on diffy's dark background. Must be
# set before Slint is configured below (it bakes the style into the compiler).
if(NOT SLINT_STYLE)
  set(SLINT_STYLE "fluent-dark" CACHE STRING "Slint widget style" FORCE)
endif()

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

# libgit2: prefer its CMake package config (this is what vcpkg ships, and it
# avoids needing pkg-config on Windows). Fall back to pkg-config for distro /
# Homebrew installs that only provide a .pc file.
find_package(libgit2 CONFIG QUIET)
if(libgit2_FOUND)
  # The exported target name has varied across versions (vcpkg ships
  # libgit2::libgit2package); pick whichever this install provides.
  foreach(_cand libgit2::libgit2package libgit2::libgit2 libgit2)
    if(TARGET ${_cand})
      set(DIFFY_LIBGIT2_TARGET ${_cand})
      break()
    endif()
  endforeach()
endif()
if(NOT DIFFY_LIBGIT2_TARGET)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
  set(DIFFY_LIBGIT2_TARGET PkgConfig::LIBGIT2)
endif()

add_executable(diffy-gui
  src/main.cpp
  src/repo_model.cc
  src/diff_bridge.cc)

target_include_directories(diffy-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(diffy-gui PRIVATE diffy_core Slint::Slint ${DIFFY_LIBGIT2_TARGET})

# The native "Browse for folder" dialog (IFileOpenDialog) lives in the COM/shell
# libraries.
if(WIN32)
  target_link_libraries(diffy-gui PRIVATE ole32 shell32)
endif()

# Compiles ui/app-window.slint and exposes the generated app-window.h.
slint_target_sources(diffy-gui ui/app-window.slint)

# On Windows, drop the Slint runtime DLL next to the binary.
if(WIN32)
  add_custom_command(TARGET diffy-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:diffy-gui> $<TARGET_FILE_DIR:diffy-gui>
    COMMAND_EXPAND_LISTS)
endif()
