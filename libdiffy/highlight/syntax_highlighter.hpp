#pragma once

/*
    Whole-buffer syntax highlighting.

    highlight_source() parses a buffer with the right tree-sitter grammar and
    returns, per line (row), the highlighted runs in line-local byte columns.
    The diff pipeline intersects these with its own per-line edit segments.

    Returns an empty result (every line unhighlighted) when the language is
    unknown, highlighting is disabled at build time, or the buffer looks binary
    / is larger than the size cap.
*/

#include "highlight/highlight_group.hpp"
#include "highlight/language.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace diffy {

// A highlighted run within one line, in byte columns [start, end).
struct HighlightRun {
    uint32_t start;
    uint32_t end;
    HighlightGroup group;
};

// runs[row] holds the (ordered, non-overlapping) runs for that line. Lines with
// no highlighting have an empty vector; rows beyond the buffer are absent.
using LineHighlights = std::vector<std::vector<HighlightRun>>;

LineHighlights
highlight_source(std::string_view source, Language lang);

// Largest buffer we will parse (bytes). Larger inputs are returned unhighlighted.
constexpr size_t kHighlightSizeCap = 2u * 1024u * 1024u;

}  // namespace diffy
