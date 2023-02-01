#pragma once

#include "algorithms/algorithm.hpp"
#include "config/config.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/readlines.hpp"

#include <string>

namespace diffy {

struct ColumnViewState {
    ColumnViewCharacters chars;
    ColumnViewSettings settings;
    ColumnViewTextStyle style_config;
    ColumnViewTextStyleEscapeCodes style;

    // Internal state below

    int64_t max_row_length = 0; // Automatically calculated based on terminal width
    int64_t line_number_digits_count = 4; // This is automatically adjusted depending on how many lines we show.
};

void
column_view_diff(const DiffInput<diffy::Line>& diff_input,
                 const std::vector<AnnotatedHunk>& hunks,
                 ColumnViewState& config,
                 int64_t width);

}  // namespace diffy
