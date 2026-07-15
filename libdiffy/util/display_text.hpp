#pragma once

/*
    Display-text helpers shared by diff frontends. Deliberately UI-agnostic (no
    Slint, no libgit2) so the same logic can back any frontend and be unit-tested
    on its own.
*/

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

// Display columns occupied by `s` (which carries no control/escape formatting of
// its own): one column per code point, and a tab advances to the next tab stop
// (`start_col` seeds the running column so stops line up when `s` is a fragment
// that begins partway across a line). This is the standalone width computation
// that backs `expand_for_display`'s column accounting — expanding `s` for display
// and measuring the result yields the same width — so frontends can size a fill
// without materialising the expanded string. Code points are counted with the
// same tolerant UTF-8 decoder as `utf8_len` (a malformed byte counts as one
// column), which matters for Latin-1 / mixed-encoding lines. `tab_width` < 1 is
// treated as 1.
int
display_width(const std::string& s, int tab_width, int start_col = 0);

// A run of display text sharing one resolved style. `argb` is a packed colour
// (0xAARRGGBB) so this stays UI-toolkit-agnostic; the bridge converts to/from
// slint::Color. Text is assumed already tab/control-expanded (expand_for_display).
struct DisplayRun {
    std::string text;
    uint32_t argb = 0;
    bool bold = false;
    uint32_t bg_argb = 0;  // per-span background (0 => transparent; used to mark changed tokens)
};

// Wrap a sequence of already-expanded styled runs into visual lines no wider than
// `wrap_cols` display columns (one column per code point, matching
// expand_for_display). Breaks after the last space when one fits; hard-breaks a
// word longer than `wrap_cols`. Style is preserved across splits and adjacent
// same-style pieces are coalesced. `wrap_cols` < 1 yields a single line (no wrap).
// Always returns at least one (possibly empty) line.
std::vector<std::vector<DisplayRun>>
wrap_display_runs(const std::vector<DisplayRun>& runs, int wrap_cols);

// Expand tabs to the next tab stop and replace control characters with spaces,
// producing text that renders with stable columns (a renderer may have no glyph
// for tabs / control chars). `col` is the running display column; it is advanced
// in place so tab stops line up across consecutive calls on the same line. One
// display column is counted per code point (UTF-8 continuation bytes don't
// advance the column). `tab_width` < 1 is treated as 1.
//
// When `show_ws` is set, whitespace is made visible (like the CLI): each space
// becomes a middot (·) and each tab becomes an arrow (→) plus fill to the tab stop.
// Column advancement is identical either way, so layout/wrapping is unaffected.
std::string
expand_for_display(const std::string& in, int tab_width, int& col, bool show_ws = false);

// True when the display codepoint at s[i] is whitespace to be rendered dim: a
// space, or the middot (·) / tab arrow (→) that expand_for_display emits under
// show-whitespace. Pairs with expand_for_display's ·/→ output.
bool
ws_glyph_at(const std::string& s, std::size_t i);

// True when every codepoint in `s` is a whitespace glyph (used by the token-band
// gap fill, which must treat a run of middots the same as a run of spaces).
bool
all_ws_glyphs(const std::string& s);

}  // namespace diffy
