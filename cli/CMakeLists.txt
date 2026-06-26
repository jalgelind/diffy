# diffy — the terminal application. Holds only CLI-specific code: argument
# parsing (getopt), terminal width/colour detection (tty), and the ANSI output
# loop. All diffing comes from diffy_core.
add_executable(diffy
  diffy_main.cc
  tty.cc)

target_include_directories(diffy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diffy PRIVATE diffy_core getopt)
target_compile_definitions(diffy PRIVATE
  DIFFY_VERSION="${PROJECT_VERSION}"
  DIFFY_BUILD_HASH="${DIFFY_BUILD_HASH}")
