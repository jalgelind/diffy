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
    std::string font_family = "monospace";  // diff (monospace); GUI maps to a per-OS family
    std::string ui_font;                     // chrome/sidebar font ("" -> per-OS default, e.g. Segoe UI)
    int64_t font_size = 13;
    bool word_wrap = false;  // off by default so syntax/token colours always show
    bool show_line_numbers = true;
    bool group_by_folder = true;  // file list: nested directory tree (default) vs flat
    bool syntax_highlight = true;  // tree-sitter syntax highlighting in the diff
    bool ignore_whitespace = false;
    bool token_granularity = true;  // token- vs line-level intra-change highlighting
    int64_t context_lines = 3;
    int64_t algorithm = 0;  // 0 patience, 1 myers-greedy, 2 myers-linear
    int64_t tab_width = 4;
    int64_t window_width = 1280;
    int64_t window_height = 800;
    // Persisted layout: sidebar width and the height of its RECENT COMMITS pane.
    int64_t sidebar_width = 300;
    int64_t commits_panel_height = 150;
    bool restore_last_repo = true;
    // Persisted PR-review connection (non-secret): the Atlassian account email used
    // for Bitbucket Basic auth. The token itself lives only in the OS credential
    // vault, keyed by this account. Empty => no persisted connection. Kept for
    // migration into the multi-account lists below.
    std::string bitbucket_account;

    // Multi-account PR-review connections (non-secret): a comma-separated list of
    // account labels per provider, plus the active label. Tokens live only in the
    // OS credential vault, keyed by (provider, base_url, label).
    std::string github_accounts;
    std::string github_active;
    std::string bitbucket_accounts;
    std::string bitbucket_active;

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
