#pragma once

/*
    Language detection and the tree-sitter grammar loader.

    A language is identified by its grammar name (e.g. "cpp", "c_sharp") — the
    file stem of a grammar shared library. Grammars are loaded at runtime from
    the first hit among:
        <exe dir>/grammars/<name>.<dll|dylib|so>   (installed layout)
        <exe dir>/../grammars/...                  (build-tree layout)
        <config dir>/grammars/...                  (user drop-ins)
    with the composed highlights query beside it as <name>.scm. Unknown or
    unloadable grammars degrade to no highlighting.
*/

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace diffy {

// Grammar name; empty = unknown / no highlighting.
using Language = std::string;

// Pick a language from a file path: config overrides first (exact filename,
// then extension), then the built-in map. Returns "" when unknown.
Language
language_for_path(std::string_view path);

// Resolve a user-supplied language token (from --language / -L) to a grammar
// name. Accepts either an extension-style token ("cpp", ".py", "rb"), resolved
// through the same map as a filename, or a raw grammar name ("c_sharp",
// "typescript"), returned as-is. Empty input yields "".
Language
language_from_name(std::string_view name);

// The distinct grammar names diffy detects out of the box (sorted, de-duped) —
// e.g. for --language help. Derived from the built-in extension/filename maps,
// so it stays in sync with what language_for_path can return.
std::vector<Language>
language_list();

// Install {pattern, language} overrides from [highlight.extensions] in
// diffy.conf. Patterns starting with '.' match file extensions, anything else
// matches an exact (case-insensitive) filename. Call once at startup, before
// any diffing threads run; replaces previously installed overrides.
void
language_set_overrides(std::vector<std::pair<std::string, Language>> patterns);

// True when highlighting support was compiled in (tree-sitter available).
bool
highlighting_available();

}  // namespace diffy
