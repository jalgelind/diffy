#pragma once

#include "binary/hex_align.hpp"
#include "config/config.hpp"  // ColumnViewTextStyleEscapeCodes

#include <cstdint>
#include <string>
#include <vector>

#include <gsl/span>

namespace diffy {

// Render a binary alignment as a unified hex diff: one offset column, interleaved
// '-'/'+'/context rows, each with hex bytes and an ASCII gutter. Large equal
// regions are collapsed to `context_rows` of context around each change, with an
// "@@ ... @@" marker for the skipped span.
//
// When `style` is non-null the rows are colourised for the terminal (delete /
// insert / context backgrounds from the theme); leave it null for plain output.
// `fill_width` (> 0, colour only) pads rows out so backgrounds reach the edge.
std::vector<std::string>
hex_unified_render(gsl::span<const uint8_t> a,
                   gsl::span<const uint8_t> b,
                   const std::string& a_name,
                   const std::string& b_name,
                   const HexAlignment& alignment,
                   const ColumnViewTextStyleEscapeCodes* style = nullptr,
                   int bytes_per_row = 16,
                   int64_t context_rows = 3,
                   int64_t fill_width = 0);

}  // namespace diffy
