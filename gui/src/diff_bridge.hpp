#pragma once

/*
    Adapts the backend-agnostic DiffViewModel into the Slint-generated row model
    the UI binds to, resolving each SpanStyle to a concrete colour for the active
    theme variant. This is the only place GUI colour decisions live.
*/

#include "app-window.h"  // generated: DiffRowData, DiffSpan, ...
#include "config/gui_config.hpp"
#include "render/diff_view_model.hpp"

#include <memory>

namespace diffy::gui {

// The Slint row list plus the widest line (in display columns) it contains,
// which the UI uses to size the horizontal scroll extent.
struct RowModel {
    std::shared_ptr<slint::VectorModel<DiffRowData>> rows;
    int max_cols = 0;
};

// Convert a laid-out diff model into a Slint VectorModel of rows. Tabs and
// control characters in the source are expanded/sanitized here so the text
// renders without missing-glyph boxes.
RowModel
build_row_model(const diffy::DiffViewModel& model, const diffy::GuiSettings& settings);

}  // namespace diffy::gui
