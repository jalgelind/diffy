#pragma once

/*
    Tree-sitter "hunk context": the enclosing definition (function, class,
    method, …) for a given line — the structural analog of git's
    `@@ … @@ <funcname>` header.

    scope_outline() parses a buffer once and returns every definition span;
    enclosing_scope() then resolves the innermost span containing a line. Both
    return empty when highlighting is disabled, the language is unknown, or the
    buffer is oversized/binary.
*/

#include "highlight/language.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace diffy {

struct CodeScope {
    int64_t start_line;  // 0-based, inclusive
    int64_t end_line;    // 0-based, inclusive
    std::string label;   // the definition's header line, trimmed (e.g. a signature)
};

// All definition/scope spans in `source`, in document order.
std::vector<CodeScope>
scope_outline(std::string_view source, Language lang);

// Label of the innermost scope containing `line` (0-based), or "" if none.
std::string
enclosing_scope(const std::vector<CodeScope>& outline, int64_t line);

// Resolve a hunk's context label. Uses the first *changed* line (not the hunk's
// leading context lines), preferring the new side, then the old side, then
// falling back to the hunk's start lines. Pass -1 for a missing changed line.
std::string
hunk_context(const std::vector<CodeScope>& a_outline,
             const std::vector<CodeScope>& b_outline,
             int64_t a_change_line,
             int64_t b_change_line,
             int64_t a_start_line,
             int64_t b_start_line);

}  // namespace diffy
