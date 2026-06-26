# libdiffy — the backend-agnostic diff core shared by the CLI and the GUI.
# It must not depend on any terminal (tty), Slint, or git code.
add_library(diffy_core STATIC
  processing/tokenizer.cc
  processing/diff_hunk.cc
  processing/diff_hunk_annotate.cc
  output/unified.cc
  output/column_view.cc
  render/diff_view_model.cc
  render/diff_pipeline.cc
  config/config.cc
  config/repos.cc
  config/gui_config.cc
  util/color.cc
  util/readlines.cc
  util/utf8decode.cc
  util/hash.cc)

# The directory itself is the include root, so module-relative includes such as
# "algorithms/algorithm.hpp" and "config/config.hpp" resolve for every consumer.
target_include_directories(diffy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# platform_folders' own target exposes its inner sago/ dir; we include it as
# <sago/platform_folders.h>, so add the parent as a search root.
target_include_directories(diffy_core PUBLIC ${CMAKE_SOURCE_DIR}/subprojects/platform_folders)
target_compile_features(diffy_core PUBLIC cxx_std_20)
target_link_libraries(diffy_core
  PUBLIC
    fmt::fmt
    Microsoft.GSL::GSL
    crc32c
    config_parser
    platform_folders)
