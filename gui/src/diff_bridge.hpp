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

// Convert a laid-out diff model into a Slint VectorModel of rows.
std::shared_ptr<slint::VectorModel<DiffRowData>>
build_row_model(const diffy::DiffViewModel& model, const diffy::GuiSettings& settings);

}  // namespace diffy::gui
