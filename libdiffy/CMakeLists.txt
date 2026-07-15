# libdiffy — the backend-agnostic diff core used by the CLI (and embeddable as a
# library by any other frontend).
# It must not depend on any terminal (tty) or git code.
add_library(diffy_core STATIC
  processing/tokenizer.cc
  processing/diff_hunk.cc
  processing/diff_hunk_annotate.cc
  processing/indent_heuristic.cc
  output/unified.cc
  output/column_view.cc
  output/hex_unified.cc
  output/hex_column.cc
  binary/chunker.cc
  binary/hex_align.cc
  image/image_info.cc
  image/decode.cc
  image/image_diff.cc
  image/term_image.cc
  render/diff_view_model.cc
  render/diff_pipeline.cc
  render/hex_view_model.cc
  highlight/highlight_group.cc
  highlight/highlight_palette.cc
  highlight/language.cc
  highlight/syntax_highlighter.cc
  highlight/scope.cc
  config/config.cc
  util/color.cc
  util/display_text.cc
  util/readlines.cc
  util/read_bytes.cc
  util/mapped_file.cc
  util/binary_detect.cc
  util/utf8decode.cc
  util/hash.cc)

# The directory itself is the include root, so module-relative includes such as
# "algorithms/algorithm.hpp" and "config/config.hpp" resolve for every consumer.
target_include_directories(diffy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# platform_folders' own target exposes its inner sago/ dir; we include it as
# <sago/platform_folders.h>, so add the parent as a search root.
target_include_directories(diffy_core PUBLIC ${DIFFY_ROOT_DIR}/subprojects/platform_folders)
# stb_image (single-header, vendored) for image decoding — private to diffy_core.
target_include_directories(diffy_core PRIVATE ${DIFFY_ROOT_DIR}/subprojects/stb)
target_compile_features(diffy_core PUBLIC cxx_std_20)
target_link_libraries(diffy_core
  PUBLIC
    fmt::fmt
    Microsoft.GSL::GSL
    crc32c
    config_parser
    platform_folders)

# Syntax highlighting via tree-sitter (fetched + built from source). Optional so
# the core can build without a network / for minimal builds.
option(DIFFY_ENABLE_HIGHLIGHT "Build syntax highlighting (tree-sitter)" ON)
if(DIFFY_ENABLE_HIGHLIGHT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/highlight/treesitter.cmake)
  # Only the runtime is linked in; grammars build as shared libraries under
  # <build>/grammars/ and are loaded on first use (see highlight/language.hpp).
  target_link_libraries(diffy_core PUBLIC tree-sitter ${CMAKE_DL_LIBS})
  target_compile_definitions(diffy_core PUBLIC DIFFY_ENABLE_HIGHLIGHT=1)
  add_dependencies(diffy_core diffy_grammars)
endif()
