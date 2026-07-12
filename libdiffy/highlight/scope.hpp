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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace diffy {

struct CodeScope {
    int64_t start_line;  // 0-based, inclusive
    int64_t end_line;    // 0-based, inclusive
    std::string label;   // the definition's header line, trimmed (e.g. a signature)
    std::string name;    // the defined identifier (unqualified), or "" if none found
};

// All definition/scope spans in `source`, in document order.
std::vector<CodeScope>
scope_outline(std::string_view source, Language lang);

// The definition named `name` (exact, unqualified match), preferring the one
// nearest `near_line` (pass -1 for none). Nullopt when no scope matches. Purely
// name-based: no scoping/shadowing awareness — the nearest same-named def wins.
std::optional<CodeScope>
resolve_definition(const std::vector<CodeScope>& outline, std::string_view name, int64_t near_line = -1);

// A local (in-function) binding — a parameter or variable declaration — carrying
// the lexical scope it is visible in, so a use can be resolved shadowing-correctly
// (innermost enclosing scope wins). Heuristic: no per-grammar `locals.scm`, just a
// declaration-node walk. Tuned for C/C++ where declarations are syntactically
// explicit; other languages get best-effort capture.
struct LocalDef {
    int64_t line;         // 0-based declaration line (the jump target)
    int64_t scope_start;  // enclosing lexical scope, inclusive (for shadowing)
    int64_t scope_end;
    std::string label;    // declaration text, trimmed (the hover signature)
    std::string name;     // the bound identifier (unqualified)
};

// Every local binding in `source`, in document order.
std::vector<LocalDef>
local_defs(std::string_view source, Language lang);

// The binding named `name` visible at `use_line`: only bindings whose scope
// encloses the use are eligible; the innermost enclosing scope wins (shadowing),
// with the nearest declaration as a tiebreak. Nullopt when none is visible.
std::optional<LocalDef>
resolve_local(const std::vector<LocalDef>& defs, std::string_view name, int64_t use_line);

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
