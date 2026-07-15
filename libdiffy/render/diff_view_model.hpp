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

#include <map>
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
    ContextGap,  // the tail gap's expander band (non-tail gaps ride their @@ header)
};

struct DiffRow {
    RowKind kind = RowKind::Content;
    std::optional<int64_t> old_lineno;  // A-side (old) line number, if any
    std::optional<int64_t> new_lineno;  // B-side (new) line number, if any
    DiffCell left;                      // unified: the content; side-by-side: old side
    DiffCell right;                     // side-by-side: new side; unified: unused
    std::string header_text;            // populated only for HunkHeader rows
    // Moved-block info for a pure delete/insert row (GAP-9): move_id pairs the two
    // ends; move_line is the 1-based counterpart line to point an arrow/reference at.
    // move_file names the counterpart file for a cross-file move (empty = same file);
    // it's filled by the frontend's all-files pass, not the per-file engine.
    int move_id = 0;
    int64_t move_line = 0;
    std::string move_file;
    // Context-gap expander metadata. gap_id identifies which run of hidden common
    // lines this row's expand/collapse controls act on (0..hunks.size(); gap g precedes
    // hunk g, gap N is the tail — see build_diff_view); gap_hidden is how many common
    // lines are still hidden behind it. Carried by the HunkHeader row that FOLLOWS a
    // non-tail gap (its @@ line hosts the controls) and by the lone ContextGap marker
    // row emitted for the tail gap. Left at -1/0 on all other rows.
    int gap_id = -1;
    int64_t gap_hidden = 0;
};

// Options that change the diff itself: flipping one must re-run compute+annotate.
struct DiffPipelineOptions {
    Algo algorithm = Algo::kPatience;
    int64_t context_lines = 3;
    EditGranularity granularity = EditGranularity::Token;
    bool ignore_whitespace = false;
    bool ignore_line_endings = false;
    bool syntax_highlight = true;  // run tree-sitter highlighting when available
    // Force the highlight grammar for both sides instead of inferring it from
    // the file names (the --language / -L equivalent). Accepts anything
    // language_from_name accepts; empty = auto-detect.
    std::string force_language;
};

// Options that only change presentation: flipping one only re-runs build_diff_view.
struct DiffLayoutOptions {
    ViewMode mode = ViewMode::SideBySide;
};

struct DiffViewModel {
    ViewMode mode = ViewMode::SideBySide;
    std::vector<DiffRow> rows;
};

// How much of one context gap the frontend has expanded (GitHub-style "expand
// context"). `top` = common lines revealed at the gap's start, adjacent to the
// PREVIOUS hunk; `bot` = lines revealed at the gap's end, adjacent to the NEXT
// hunk. Both are clamped to the gap's real hidden size inside build_diff_view.
struct GapExpansion {
    int64_t top = 0;
    int64_t bot = 0;
};

// Pure: lays out already-annotated hunks into the chosen view. No I/O.
// When per-line syntax highlights are supplied (a_highlights for the A/old
// side, b_highlights for the B/new side), spans are additionally split at
// syntax boundaries and tagged with their HighlightGroup.
// Between the hunks (and above the first / below the last) sit runs of hidden
// common lines. A non-tail gap folds its expander metadata (gap_id/gap_hidden) onto
// the HunkHeader row that follows it, so the @@ line can host the controls; the tail
// gap (after the last hunk) has no following header, so it emits a lone ContextGap
// marker row. `expansions` (keyed by gap_id) reveals `top`/`bot` of a gap's lines so
// the user can widen the context incrementally. Null => all gaps collapsed.
DiffViewModel
build_diff_view(const DiffInput<Line>& input,
                const std::vector<AnnotatedHunk>& hunks,
                const DiffLayoutOptions& options,
                const LineHighlights* a_highlights = nullptr,
                const LineHighlights* b_highlights = nullptr,
                const std::map<int, GapExpansion>* expansions = nullptr);

// One file's built diff, for cross-file move detection.
struct CrossFileDiff {
    std::string path;
    DiffViewModel* model;  // mutated in place: move_id/move_line/move_file get set
};

// GAP-9 cross-file moves: across several files' diffs, a run of >= 3 pure-deleted
// lines in one file whose content matches a run of pure-inserted lines in ANOTHER
// file is tagged as a move (shared move_id, and each end's move_file/move_line points
// at the counterpart). Same-file moves are handled per file by detect_moves(); this
// only considers still-unmatched (move_id == 0) rows. Pure logic — no I/O, no threads.
void
detect_cross_file_moves(const std::vector<CrossFileDiff>& files);

}  // namespace diffy
