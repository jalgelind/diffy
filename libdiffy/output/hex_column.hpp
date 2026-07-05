#pragma once

#include "binary/hex_align.hpp"
#include "config/config.hpp"  // ColumnViewTextStyleEscapeCodes

#include <cstdint>
#include <string>
#include <vector>

#include <gsl/span>

namespace diffy {

// Render a binary alignment as a side-by-side hex diff: two panes (offset + hex +
// ASCII) sharing logical rows, with gaps padded so equal bytes stay column
// aligned. Changed bytes are coloured per side (removed = delete colour on the
// left, added = insert colour on the right). Large equal regions collapse to
// `context_rows` of context around each change with an "@@ ... @@" marker.
//
// `width` (> 0) is the available terminal width; bytes-per-row is reduced to fit
// when necessary. `style` null => plain output.
std::vector<std::string>
hex_column_render(gsl::span<const uint8_t> a,
                  gsl::span<const uint8_t> b,
                  const HexAlignment& alignment,
                  const ColumnViewTextStyleEscapeCodes* style = nullptr,
                  int bytes_per_row = 16,
                  int64_t context_rows = 3,
                  int64_t width = 0);

}  // namespace diffy
