#pragma once

/*
    Adapts the backend-agnostic DiffViewModel into the Slint-generated row model
    the UI binds to, resolving each SpanStyle to a concrete colour for the active
    theme variant. This is the only place GUI colour decisions live.
*/

#include "app-window.h"  // generated: DiffRowData, DiffSpan, ...
#include "render/diff_view_model.hpp"

#include <memory>
#include <string>

namespace diffy::gui {

// GUI colours resolved from a diffy theme .conf (the same files the CLI uses).
// Each is a concrete RGB colour; load_gui_theme() converts the theme's TermColors.
struct GuiTheme {
    slint::Color bg;            // window / common-line background
    slint::Color panel_bg;     // sidebar / chrome
    slint::Color fg;           // default text
    slint::Color gutter_fg;    // line numbers / muted text
    slint::Color accent;       // ui accent (from the header colour)
    slint::Color header_bg;    // hunk-header background
    slint::Color header_fg;    // hunk-header text
    slint::Color delete_bg;    // deleted-line cell tint
    slint::Color insert_bg;    // inserted-line cell tint
    slint::Color delete_token; // changed-token foreground (removed)
    slint::Color insert_token; // changed-token foreground (added)
    bool dark = true;          // background is dark -> pick the dark syntax palette
};

// Load and resolve a theme .conf (e.g. "theme_github_light") into GuiTheme,
// using the same loader as the CLI. Falls back to sensible defaults for any
// colour the theme leaves at the terminal default.
GuiTheme
load_gui_theme(const std::string& theme_name);

// The Slint row list plus the widest line (in display columns) it contains,
// which the UI uses to size the horizontal scroll extent.
struct RowModel {
    std::shared_ptr<slint::VectorModel<DiffRowData>> rows;
    int max_cols = 0;
};

// Convert a laid-out diff model into a Slint VectorModel of rows, coloured by
// `theme`. Tabs and control characters are expanded/sanitized so the text
// renders without missing-glyph boxes.
RowModel
build_row_model(const diffy::DiffViewModel& model, const GuiTheme& theme, int tab_width);

}  // namespace diffy::gui
