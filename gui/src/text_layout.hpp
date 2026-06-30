#pragma once

/*
    Display-text helpers shared by the diff bridge. Deliberately UI-agnostic (no
    Slint, no libgit2) so the same logic can back a non-Slint frontend and be
    unit-tested on its own.
*/

#include <cstdint>
#include <string>
#include <vector>

namespace diffy::gui {

// A run of display text sharing one resolved style. `argb` is a packed colour
// (0xAARRGGBB) so this stays UI-toolkit-agnostic; the bridge converts to/from
// slint::Color. Text is assumed already tab/control-expanded (expand_for_display).
struct DisplayRun {
    std::string text;
    uint32_t argb = 0;
    bool bold = false;
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
std::string
expand_for_display(const std::string& in, int tab_width, int& col);

}  // namespace diffy::gui
