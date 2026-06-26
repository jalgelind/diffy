#pragma once

/*
    Language detection and the tree-sitter binding for each language.

    The set of supported languages is intentionally data-driven: each entry maps
    to a tree-sitter grammar (linked in via treesitter.cmake) and a highlight
    query (embedded by the same). Adding a language is a registry entry plus a
    line in treesitter.cmake.
*/

#include <string>
#include <string_view>

namespace diffy {

enum class Language {
    None,
    C,
    Cpp,
    Go,
    Rust,
    Java,
    CSharp,
    Python,
    Ruby,
    Bash,
    JavaScript,
    TypeScript,
    Tsx,
    Json,
    // (expanded in the grammar tasks)
};

// Pick a language from a file path (extension, then special filenames).
// Returns Language::None when unknown / unsupported.
Language
language_for_path(std::string_view path);

// True when highlighting support was compiled in (tree-sitter available).
bool
highlighting_available();

}  // namespace diffy
