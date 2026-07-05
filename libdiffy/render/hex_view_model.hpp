#pragma once

/*
    Structured (backend-agnostic) hex diff view.

    build_hex_view() turns a binary alignment into the same DiffViewModel the text
    diff produces, so a frontend that already renders DiffViewModel (the GUI) can
    show hex diffs with no new rendering code. It mirrors what the CLI's
    hex_unified_render / hex_column_render produce, but as StyledSpans instead of
    ANSI: equal bytes are SpanStyle::Common, removed bytes DeleteToken, added
    bytes InsertToken. Byte offsets are embedded as leading Common spans (the
    line-number gutters stay empty, since offsets aren't line numbers).
*/

#include "binary/hex_align.hpp"
#include "render/diff_view_model.hpp"

#include <cstdint>

#include <gsl/span>

namespace diffy {

DiffViewModel
build_hex_view(gsl::span<const uint8_t> a,
               gsl::span<const uint8_t> b,
               const HexAlignment& alignment,
               const DiffLayoutOptions& options,
               int bytes_per_row = 16,
               int64_t context_rows = 3);

}  // namespace diffy
