# Tree-sitter runtime + grammars, vendored as git submodules under subprojects/.
#
# Provides:
#   - target `tree-sitter` (the C runtime, linked statically into diffy_core)
#   - one shared library per grammar under ${CMAKE_BINARY_DIR}/grammars/
#     (<lang>.dll/.dylib/.so), loaded at runtime on first use — grammars are
#     NOT linked into the executables (their parse tables dominate binary size)
#   - a composed highlights query beside each grammar (<lang>.scm), with base
#     grammar queries prepended (INHERITS)
#   - aggregate target `diffy_grammars`
#
# add_ts_grammar(<lang> <submodule-dir> [SUBDIR <dir>] [QUERY <relpath>]
#                [LOCAL_QUERY <abs>] [INHERITS <lang>...])
#   <submodule-dir> : directory name under subprojects/ (e.g. tree-sitter-c)
#   SUBDIR : grammar lives in a subdirectory of the repo (e.g. typescript/, tsx/)
#   QUERY  : highlights query path relative to the repo (default queries/highlights.scm)
#   LOCAL_QUERY : an absolute path to a highlights query we ship (grammars w/ none)
#   INHERITS : base languages whose queries are prepended to this one's .scm
#              (e.g. cpp inherits c) — must also be add_ts_grammar'd

# The runtime and grammars are C; the top-level project only enables C++.
enable_language(C)

# Root holding the tree-sitter submodules.
set(DIFFY_TS_ROOT ${DIFFY_ROOT_DIR}/subprojects)

if(NOT EXISTS ${DIFFY_TS_ROOT}/tree-sitter/lib/src/lib.c)
  message(FATAL_ERROR
    "tree-sitter submodule not found at ${DIFFY_TS_ROOT}/tree-sitter.\n"
    "Initialize submodules first:\n"
    "    git submodule update --init --recursive\n"
    "or configure with -DDIFFY_ENABLE_HIGHLIGHT=OFF to build without highlighting.")
endif()

# --- runtime --------------------------------------------------------------
add_library(tree-sitter STATIC ${DIFFY_TS_ROOT}/tree-sitter/lib/src/lib.c)
target_include_directories(tree-sitter
  PUBLIC ${DIFFY_TS_ROOT}/tree-sitter/lib/include
  PRIVATE ${DIFFY_TS_ROOT}/tree-sitter/lib/src)
set_target_properties(tree-sitter PROPERTIES C_STANDARD 11)
# These are large generated/third-party C files; silence their warnings so a
# global -Werror/-WX can't fail the build.
if(MSVC)
  target_compile_options(tree-sitter PRIVATE /W0)
else()
  target_compile_options(tree-sitter PRIVATE -w)
endif()

# --- grammar shared libraries ----------------------------------------------
# The executables look for grammars in <exe dir>/grammars and <exe dir>/../grammars;
# building them all into ${CMAKE_BINARY_DIR}/grammars covers cli/ and tests/.
set(DIFFY_GRAMMAR_DIR ${CMAKE_BINARY_DIR}/grammars)
file(MAKE_DIRECTORY ${DIFFY_GRAMMAR_DIR})

add_custom_target(diffy_grammars)

set_property(GLOBAL PROPERTY DIFFY_TS_LANGS "")

function(add_ts_grammar lang repo_dir)
  cmake_parse_arguments(G "" "SUBDIR;QUERY;LOCAL_QUERY" "INHERITS" ${ARGN})

  set(root ${DIFFY_TS_ROOT}/${repo_dir})

  if(G_SUBDIR)
    set(gsrc ${root}/${G_SUBDIR}/src)
  else()
    set(gsrc ${root}/src)
  endif()

  if(NOT EXISTS ${gsrc}/parser.c)
    message(FATAL_ERROR
      "tree-sitter grammar '${lang}' missing at ${gsrc}.\n"
      "Run: git submodule update --init --recursive")
  endif()

  set(sources ${gsrc}/parser.c)
  if(EXISTS ${gsrc}/scanner.c)
    list(APPEND sources ${gsrc}/scanner.c)
  elseif(EXISTS ${gsrc}/scanner.cc)
    list(APPEND sources ${gsrc}/scanner.cc)
  endif()

  # MODULE: dlopen/LoadLibrary-only. parser.c marks tree_sitter_<lang>() with
  # TS_PUBLIC (__declspec(dllexport) / default visibility), so no export lists
  # are needed. Grammars are self-contained — tables plus the entry point — and
  # do not link against the tree-sitter runtime.
  add_library(ts_${lang} MODULE ${sources})
  # parser.c includes "tree_sitter/parser.h", vendored under the grammar's src.
  target_include_directories(ts_${lang} PRIVATE ${gsrc})
  set_target_properties(ts_${lang} PROPERTIES
    C_STANDARD 11
    PREFIX ""
    OUTPUT_NAME ${lang}
    SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}  # MODULE defaults to .so on macOS; the loader expects .dylib
    LIBRARY_OUTPUT_DIRECTORY ${DIFFY_GRAMMAR_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${DIFFY_GRAMMAR_DIR})
  if(MSVC)
    target_compile_options(ts_${lang} PRIVATE /W0)
  else()
    target_compile_options(ts_${lang} PRIVATE -w)
  endif()
  add_dependencies(diffy_grammars ts_${lang})

  if(G_LOCAL_QUERY)
    set(qpath ${G_LOCAL_QUERY})
  elseif(G_QUERY)
    set(qpath ${root}/${G_QUERY})
  else()
    set(qpath ${root}/queries/highlights.scm)
  endif()
  set_property(GLOBAL APPEND PROPERTY DIFFY_TS_LANGS ${lang})
  set_property(GLOBAL PROPERTY DIFFY_TS_QUERY_${lang} "${qpath}")
  set_property(GLOBAL PROPERTY DIFFY_TS_CHAIN_${lang} "${G_INHERITS}")
endfunction()

# Writes each grammar's composed highlights query (bases first, own last) to
# ${DIFFY_GRAMMAR_DIR}/<lang>.scm, next to the grammar library.
function(finalize_ts_queries)
  get_property(langs GLOBAL PROPERTY DIFFY_TS_LANGS)
  foreach(lang ${langs})
    get_property(chain GLOBAL PROPERTY DIFFY_TS_CHAIN_${lang})
    list(APPEND chain ${lang})
    set(content "")
    foreach(dep ${chain})
      get_property(qpath GLOBAL PROPERTY DIFFY_TS_QUERY_${dep})
      if(qpath AND EXISTS ${qpath})
        file(READ ${qpath} q)
        string(APPEND content "${q}\n")
      endif()
    endforeach()
    file(WRITE ${DIFFY_GRAMMAR_DIR}/${lang}.scm "${content}")
  endforeach()
endfunction()

# --- the grammar set ------------------------------------------------------
# Each <dir> is a submodule under subprojects/, pinned to a tag from the
# tree-sitter 0.21/0.22 era for language-ABI 14 compatibility with the runtime.
add_ts_grammar(c          tree-sitter-c)
add_ts_grammar(cpp        tree-sitter-cpp INHERITS c)  # C++ inherits C highlights
add_ts_grammar(go         tree-sitter-go)
add_ts_grammar(rust       tree-sitter-rust)
add_ts_grammar(java       tree-sitter-java)
add_ts_grammar(python     tree-sitter-python)
add_ts_grammar(javascript tree-sitter-javascript)
add_ts_grammar(typescript tree-sitter-typescript SUBDIR typescript INHERITS javascript)
add_ts_grammar(tsx        tree-sitter-typescript SUBDIR tsx INHERITS javascript)
add_ts_grammar(ruby       tree-sitter-ruby)
add_ts_grammar(bash       tree-sitter-bash)
add_ts_grammar(c_sharp    tree-sitter-c-sharp)
add_ts_grammar(html       tree-sitter-html)
add_ts_grammar(css        tree-sitter-css)
add_ts_grammar(lua        tree-sitter-lua)
add_ts_grammar(toml       tree-sitter-toml)
add_ts_grammar(cmake      tree-sitter-cmake
               LOCAL_QUERY ${CMAKE_CURRENT_SOURCE_DIR}/highlight/queries/cmake.scm)
add_ts_grammar(markdown   tree-sitter-markdown
               SUBDIR tree-sitter-markdown QUERY tree-sitter-markdown/queries/highlights.scm)
add_ts_grammar(json       tree-sitter-json)

# More languages can be added with an add_ts_grammar line here plus a built-in
# extension entry in language.cc — or with no rebuild at all: drop a grammar
# library + .scm into <config dir>/grammars/ and map its extensions under
# [highlight.extensions] in diffy.conf.

finalize_ts_queries()
