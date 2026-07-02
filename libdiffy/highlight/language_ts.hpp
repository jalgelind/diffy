#pragma once

// Internal tree-sitter bindings — used by the highlighter, not by general
// consumers (which only need language.hpp).

#include "highlight/language.hpp"

#include <string>

struct TSLanguage;  // opaque, defined by tree-sitter

namespace diffy {

// The grammar for `lang`, loaded on first use from a grammar shared library
// (see language.hpp), or nullptr if unavailable / highlighting disabled.
const TSLanguage*
ts_language_for(const Language& lang);

// The composed highlights query for `lang` (the shipped <lang>.scm has base
// grammars prepended at build time), or empty if unavailable.
std::string
highlight_query_for(const Language& lang);

}  // namespace diffy
