#pragma once

/*
    Backend-agnostic diff render model.

    build_diff_view() turns annotated hunks into a flat list of rows made of
    semantically-styled text spans — no ANSI escapes, no terminal width, no
    backend-specific types. Frontends serialise it however they like:
      - the CLI maps SpanStyle -> ANSI (see cli/),
      - a graphical frontend maps SpanStyle -> RGB + font attributes.

    The same model expresses both unified and side-by-side layouts so a frontend
    can switch views without re-running the diff.
*/

#include "algorithms/algorithm.hpp"
#include "highlight/highlight_group.hpp"
#include "highlight/syntax_highlighter.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/readlines.hpp"

#include <optional>
#include <string>
#include <vector>

namespace diffy {

enum class ViewMode {
    Unified,
    SideBySide,
};

// Semantic style of a run of text. The concrete colour/attributes come from the
// theme, resolved by each frontend.
enum class SpanStyle {
    Common,       // unchanged text
    Insert,       // inside an inserted line, unchanged-within-line text
    Delete,       // inside a deleted line, unchanged-within-line text
    InsertToken,  // the actually-added token(s) on an inserted line
    DeleteToken,  // the actually-removed token(s) on a deleted line
    Meta,         // padding / structural
};

struct StyledSpan {
    std::string text;
    SpanStyle style = SpanStyle::Common;
    HighlightGroup syntax = HighlightGroup::None;  // tree-sitter highlight, if any
};

struct DiffCell {
    std::vector<StyledSpan> spans;
    EditType type = EditType::Common;
    bool present = false;  // false => empty padding cell (alignment gap / unused side)
};

enum class RowKind {
    HunkHeader,  // the "@@ ... @@" separator between hunks
    Content,     // a real line of diff
};

struct DiffRow {
    RowKind kind = RowKind::Content;
    std::optional<int64_t> old_lineno;  // A-side (old) line number, if any
    std::optional<int64_t> new_lineno;  // B-side (new) line number, if any
    DiffCell left;                      // unified: the content; side-by-side: old side
    DiffCell right;                     // side-by-side: new side; unified: unused
    std::string header_text;            // populated only for HunkHeader rows
};

// Options that change the diff itself: flipping one must re-run compute+annotate.
struct DiffPipelineOptions {
    Algo algorithm = Algo::kPatience;
    int64_t context_lines = 3;
    EditGranularity granularity = EditGranularity::Token;
    bool ignore_whitespace = false;
    bool ignore_line_endings = false;
    bool syntax_highlight = true;  // run tree-sitter highlighting when available
};

// Options that only change presentation: flipping one only re-runs build_diff_view.
struct DiffLayoutOptions {
    ViewMode mode = ViewMode::SideBySide;
};

struct DiffViewModel {
    ViewMode mode = ViewMode::SideBySide;
    std::vector<DiffRow> rows;
};

// Pure: lays out already-annotated hunks into the chosen view. No I/O.
// When per-line syntax highlights are supplied (a_highlights for the A/old
// side, b_highlights for the B/new side), spans are additionally split at
// syntax boundaries and tagged with their HighlightGroup.
DiffViewModel
build_diff_view(const DiffInput<Line>& input,
                const std::vector<AnnotatedHunk>& hunks,
                const DiffLayoutOptions& options,
                const LineHighlights* a_highlights = nullptr,
                const LineHighlights* b_highlights = nullptr);

}  // namespace diffy
