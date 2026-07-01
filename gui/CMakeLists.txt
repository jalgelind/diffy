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

# libgit2: on Windows prefer its CMake package config (what vcpkg ships, and it
# avoids needing pkg-config there). On Unix/macOS prefer pkg-config — Homebrew's
# libgit2 CMake config bakes absolute SDK paths recorded on the brew build host
# (e.g. .../CommandLineTools/SDKs/MacOSX26.sdk/usr/lib/libz.tbd) that don't exist
# when the active toolchain is Xcode, breaking the link. pkg-config yields a
# clean `-lgit2` instead.
set(DIFFY_LIBGIT2_TARGET "")
if(WIN32)
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
endif()
if(NOT DIFFY_LIBGIT2_TARGET)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
  set(DIFFY_LIBGIT2_TARGET PkgConfig::LIBGIT2)
endif()

# --- diffy_review: backend-agnostic PR-review layer (Slint-free) -----------
# Vendored header-only JSON, then the review static lib. Kept free of Slint (and
# of diffy_core) so it can be unit-tested by diffy-gui-logic-test and reused by a
# future CLI. HTTP backends, secret store, providers etc. are added by later
# tasks; see REVIEW-ROADMAP.md.
add_subdirectory(${CMAKE_SOURCE_DIR}/subprojects/json
                 ${CMAKE_BINARY_DIR}/subprojects/json EXCLUDE_FROM_ALL)

# Platform-neutral review sources + the OS backend for HTTP and secret storage.
# The mac backends are Objective-C++ (.mm), which only Apple's toolchain can
# compile — so select sources per-platform rather than listing all and relying on
# #if guards (MSVC can't process a .mm at all).
set(DIFFY_REVIEW_SOURCES
  src/review/review.cc
  src/review/secret_store.cc
  src/review/link_header.cc)
if(WIN32)
  list(APPEND DIFFY_REVIEW_SOURCES src/review/http_win.cpp src/review/secret_store_win.cpp)
elseif(APPLE)
  enable_language(OBJCXX)
  list(APPEND DIFFY_REVIEW_SOURCES src/review/http_mac.mm src/review/secret_store_mac.mm)
else()
  list(APPEND DIFFY_REVIEW_SOURCES src/review/http_curl.cc)
endif()

add_library(diffy_review STATIC ${DIFFY_REVIEW_SOURCES})
target_include_directories(diffy_review PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(diffy_review PUBLIC nlohmann_json::nlohmann_json)

# System libs for the backends (PUBLIC so they propagate through the static lib to
# the final exe link). Windows: WinHTTP + Credential Manager. macOS: Foundation +
# Keychain/Security. Linux: libcurl.
if(WIN32)
  target_link_libraries(diffy_review PUBLIC winhttp Advapi32)
elseif(APPLE)
  target_link_libraries(diffy_review PUBLIC
    "-framework Foundation" "-framework Security" "-framework CoreFoundation")
else()
  find_package(CURL REQUIRED)
  target_link_libraries(diffy_review PUBLIC CURL::libcurl)
endif()

add_executable(diffy-gui
  src/main.cpp
  src/repo_model.cc
  src/diff_bridge.cc
  src/text_layout.cc)

target_include_directories(diffy-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(diffy-gui PRIVATE diffy_core diffy_review Slint::Slint ${DIFFY_LIBGIT2_TARGET})

# Slint's winit/skia backend is built as a static Rust library; the macOS
# frameworks and the ObjC runtime its objc2/winit/skia/accesskit crates call
# into aren't propagated to the final link, so name them explicitly. Without
# these the link fails with undefined ObjC runtime symbols (_objc_msgSend,
# _sel_registerName, …) and missing AppKit/Foundation/Metal classes.
if(APPLE)
  foreach(_fw AppKit Foundation CoreFoundation CoreGraphics CoreText
              QuartzCore Metal MetalKit IOSurface CoreVideo Security
              OpenGL Carbon)
    find_library(DIFFY_FW_${_fw} ${_fw} REQUIRED)
    target_link_libraries(diffy-gui PRIVATE ${DIFFY_FW_${_fw}})
  endforeach()
  target_link_libraries(diffy-gui PRIVATE objc)
endif()

# The native "Browse for folder" dialog (IFileOpenDialog) lives in the COM/shell
# libraries.
if(WIN32)
  target_link_libraries(diffy-gui PRIVATE ole32 shell32 dwmapi)
  # GUI-subsystem app so launching it doesn't pop a console window. Keep the
  # standard int main() entry point under MSVC's /SUBSYSTEM:WINDOWS.
  set_target_properties(diffy-gui PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(diffy-gui PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()

# Compiles ui/app-window.slint and exposes the generated app-window.h.
slint_target_sources(diffy-gui ui/app-window.slint)

# On Windows, drop the Slint runtime DLL next to the binary.
if(WIN32)
  add_custom_command(TARGET diffy-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:diffy-gui> $<TARGET_FILE_DIR:diffy-gui>
    COMMAND_EXPAND_LISTS)
endif()

# A Slint-free test of the reusable git layer (repo_model). It links only
# libgit2 — no Slint, no diffy_core — which is exactly the point: it proves the
# repository logic can be driven by a non-Slint frontend, and exercises the
# write operations against throwaway temp repos. Builds fast (no Rust/Slint).
if(DIFFY_BUILD_TESTS)
  add_executable(diffy-gui-logic-test
    src/repo_model.cc
    src/text_layout.cc
    tests/repo_model_tests.cc
    tests/text_layout_tests.cc
    tests/review_tests.cc)
  target_include_directories(diffy-gui-logic-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/subprojects/doctest/doctest)
  target_link_libraries(diffy-gui-logic-test PRIVATE diffy_review ${DIFFY_LIBGIT2_TARGET})
  add_test(NAME gui-logic-tests COMMAND diffy-gui-logic-test)
endif()
