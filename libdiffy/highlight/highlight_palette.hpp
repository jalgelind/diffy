#pragma once

/*
    The default colour for each HighlightGroup, for the dark and light variants.
    Living in the core keeps the CLI and GUI in sync. Frontends consume the RGB
    directly (GUI) or convert it to a terminal style (CLI). Reading per-group
    overrides from the user's theme file is a future enhancement; this provides
    a clean, VS-Code-ish default.
*/

#include "highlight/highlight_group.hpp"

#include <cstdint>

namespace diffy {

struct HlRgb {
    uint8_t r, g, b;
};

// Foreground colour for `group` in the chosen variant. HighlightGroup::None
// returns the theme's default foreground. Honours any override set below.
HlRgb
syntax_color(HighlightGroup group, bool light_theme);

// Override the colour for a group (applies to both variants), e.g. from the
// user's theme/config. Frontends call this at startup. HighlightGroup::None is
// ignored. Overrides persist for the process; clear_syntax_overrides() resets.
void
set_syntax_color_override(HighlightGroup group, HlRgb rgb);

void
clear_syntax_overrides();

}  // namespace diffy
