#pragma once

/*
    Display-text helpers shared by the diff bridge. Deliberately UI-agnostic (no
    Slint, no libgit2) so the same logic can back a non-Slint frontend and be
    unit-tested on its own.
*/

#include <string>

namespace diffy::gui {

// Expand tabs to the next tab stop and replace control characters with spaces,
// producing text that renders with stable columns (a renderer may have no glyph
// for tabs / control chars). `col` is the running display column; it is advanced
// in place so tab stops line up across consecutive calls on the same line. One
// display column is counted per code point (UTF-8 continuation bytes don't
// advance the column). `tab_width` < 1 is treated as 1.
std::string
expand_for_display(const std::string& in, int tab_width, int& col);

}  // namespace diffy::gui
