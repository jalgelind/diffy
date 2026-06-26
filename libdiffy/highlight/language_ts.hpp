#pragma once

// Internal tree-sitter bindings — used by the highlighter, not by general
// consumers (which only need language.hpp).

#include "highlight/language.hpp"

#include <string>

struct TSLanguage;  // opaque, defined by tree-sitter

namespace diffy {

// The grammar for `lang`, or nullptr if unsupported / highlighting disabled.
const TSLanguage*
ts_language_for(Language lang);

// The composed highlights query for `lang` (base grammars prepended for
// inheritance), or empty if unavailable.
std::string
highlight_query_for(Language lang);

}  // namespace diffy
