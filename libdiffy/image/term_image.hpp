#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

// How to render an image inline in the terminal. Half-block is the universal
// truecolor fallback; kitty and iTerm2 draw crisp bitmaps (sized in cells, so
// the terminal applies its own cell metrics — no pixel-size query needed).
enum class TermImageProtocol {
    None,       // can't / shouldn't render (piped, disabled)
    HalfBlock,  // ANSI truecolor upper-half-block art
    Kitty,      // kitty graphics protocol (raw RGBA)
    ITerm2,     // iTerm2 inline image (OSC 1337, PNG payload)
};

struct TermEnv {
    bool is_tty = false;    // stdout is an interactive terminal
    bool disabled = false;  // --no-image-render / $NO_COLOR
    bool force = false;     // --image-render: render even to a pipe (half-block, for testing)
    std::string term;              // $TERM
    std::string term_program;      // $TERM_PROGRAM
    std::string kitty_window_id;   // $KITTY_WINDOW_ID
};

TermImageProtocol
detect_term_image_protocol(const TermEnv& env);

// Render an RGBA8 image (rgba is w*h*4, row-major) with `protocol`, fitting
// within max_cols x max_rows character cells. Returns "" for None / bad input.
std::string
render_term_image(TermImageProtocol protocol, const std::vector<uint8_t>& rgba, int w, int h,
                  int max_cols, int max_rows);

// Half-block renderer (exposed for tests / direct use). See render_term_image.
std::string
render_halfblock(const std::vector<uint8_t>& rgba, int w, int h, int max_cols, int max_rows);

}  // namespace diffy
