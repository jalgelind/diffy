#pragma once

/*
    The GUI's slice of the shared diffy.conf: everything under the [gui] table.
    The CLI ignores this section; the GUI reads it at startup and writes it back
    when the user changes a persisted preference. All other sections (general,
    settings, chars, style, color_map) remain shared and untouched here.
*/

#include "render/diff_view_model.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace diffy {

struct GuiSettings {
    std::string default_view = "side-by-side";  // "side-by-side" | "unified"
    std::string theme = "theme_default";        // theme file to load (shared with CLI)
    std::string theme_variant = "dark";         // "dark" | "light" | "system"
    std::string font_family = "monospace";  // generic; the GUI maps it to a real per-OS family
    int64_t font_size = 13;
    bool word_wrap = true;
    bool show_line_numbers = true;
    bool syntax_highlight = true;  // tree-sitter syntax highlighting in the diff
    int64_t tab_width = 4;
    int64_t window_width = 1280;
    int64_t window_height = 800;
    bool restore_last_repo = true;

    // Optional per-group syntax colour overrides from the [gui.syntax] table,
    // as (group-name, "#rrggbb") pairs. The frontend resolves names to groups.
    std::vector<std::pair<std::string, std::string>> syntax_overrides;

    ViewMode
    view_mode() const {
        return default_view == "unified" ? ViewMode::Unified : ViewMode::SideBySide;
    }
};

// Read [gui] from diffy.conf into `settings`, filling defaults for missing keys
// and writing the file back if it had to be created/extended.
void
gui_settings_load(GuiSettings& settings);

// Persist `settings` into the [gui] table of diffy.conf, preserving the rest of
// the file (other sections and comments).
void
gui_settings_save(const GuiSettings& settings);

}  // namespace diffy
