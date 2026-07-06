#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

// How (if at all) to render an image inline in the terminal. Half-block is the
// universal truecolor fallback; crisper graphics protocols (kitty / iTerm2 /
// sixel) are a later addition — the enum leaves room for them.
enum class TermImageProtocol {
    None,       // can't / shouldn't render (piped, disabled)
    HalfBlock,  // ANSI truecolor upper-half-block art
};

struct TermEnv {
    bool is_tty = false;   // stdout is an interactive terminal
    bool disabled = false;  // --no-image-render / $NO_COLOR
    bool force = false;     // --image-render: render even to a pipe (testing)
};

TermImageProtocol
detect_term_image_protocol(const TermEnv& env);

// Render an RGBA8 image (rgba is w*h*4, row-major) as ANSI truecolor half-block
// art, scaled (preserving aspect) to fit within max_cols x max_rows character
// cells. Each cell is one column wide and two image rows tall (fg = upper pixel,
// bg = lower pixel, glyph = U+2580). Returns "" on bad input.
std::string
render_halfblock(const std::vector<uint8_t>& rgba, int w, int h, int max_cols, int max_rows);

}  // namespace diffy
