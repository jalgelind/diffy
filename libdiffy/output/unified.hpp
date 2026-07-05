#pragma once

#include "algorithms/algorithm.hpp"
#include "config/config.hpp"                 // ColumnViewTextStyleEscapeCodes
#include "highlight/syntax_highlighter.hpp"  // LineHighlights
#include "processing/diff_hunk.hpp"
#include "util/readlines.hpp"

#include <string>
#include <vector>

namespace diffy {

// When hunk_contexts is supplied (one entry per hunk; empties ignored), each
// non-empty label is appended to that hunk's "@@ ... @@" header, git-style.
//
// When `style` is non-null the output is colourised for terminal viewing: each
// content line gets its theme background (added / removed / context) and, when
// `a_hl` / `b_hl` are supplied, tree-sitter syntax foreground. Leave `style`
// null (the default) for plain, patch-compatible output — callers writing to a
// pipe/file should do so.
//
// `fill_width` (> 0) pads every coloured line with its background out to that
// many terminal columns, so added/removed/context rows read as solid bars
// instead of colour only behind the text. Ignored when `style` is null.
std::vector<std::string>
unified_diff_render(const DiffInput<Line>& diff_input,
                    const std::vector<Hunk>& hunks,
                    const std::vector<std::string>* hunk_contexts = nullptr,
                    const ColumnViewTextStyleEscapeCodes* style = nullptr,
                    const LineHighlights* a_hl = nullptr,
                    const LineHighlights* b_hl = nullptr,
                    bool light_theme = false,
                    int64_t fill_width = 0);

}  // namespace diffy
