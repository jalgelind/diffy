#pragma once

/*
    The grammar-agnostic vocabulary of syntax highlighting.

    Tree-sitter grammars emit a long tail of capture names (e.g. "keyword",
    "keyword.control.return", "function.method", "variable.parameter",
    "string.special", "punctuation.bracket"). We collapse those into a small,
    stable set of HighlightGroups that both frontends know how to colour. This
    is the *only* place capture names are interpreted; everything downstream
    speaks HighlightGroup.
*/

#include <string_view>

namespace diffy {

enum class HighlightGroup {
    None,  // not highlighted — render with the default foreground

    Comment,
    Keyword,    // keywords, including control-flow and operators-as-words
    Operator,   // + - * / etc.
    Punctuation,  // brackets, delimiters, separators

    String,
    Escape,   // escape sequences inside strings, format specifiers
    Number,   // integer / float literals
    Boolean,  // true / false
    Constant,      // user constants, enum members
    ConstantBuiltin,  // language built-in constants (nil, None, NULL, ...)

    Function,     // function names at definition / call sites
    Method,       // member functions
    Constructor,  // constructors / object initialisers

    Type,         // user types, classes, structs
    TypeBuiltin,  // built-in / primitive types

    Variable,   // identifiers
    Parameter,  // function parameters
    Property,   // object fields / members
    Namespace,  // modules / namespaces / packages
    Label,      // goto labels, loop labels

    Tag,        // markup element tags (HTML/JSX)
    Attribute,  // markup / annotation attributes
};

// Map a tree-sitter capture name (without the leading '@', possibly dotted such
// as "keyword.control.return") to a HighlightGroup. Unknown names -> None.
// Matching is most-specific-first on the dotted components.
HighlightGroup
group_for_capture(std::string_view capture);

}  // namespace diffy
