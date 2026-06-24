#pragma once

#include "algorithms/algorithm.hpp"
#include "config/config.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/readlines.hpp"

#include <string>
#include <vector>

namespace diffy {

struct ColumnViewState {
    ColumnViewCharacters chars;
    ColumnViewSettings settings;
    ColumnViewTextStyle style_config;
    ColumnViewTextStyleEscapeCodes style;

    // Internal state below

    int64_t max_row_length = 0;  // Automatically calculated based on terminal width
    int64_t line_number_digits_count =
        4;  // This is automatically adjusted depending on how many lines we show.
};

// Render the side-by-side diff into one styled string per visual row, using an
// explicit terminal width. column_view_diff_render() wraps this with terminal
// width detection and prints each line.
std::vector<std::string>
column_view_render_lines(const DiffInput<diffy::Line>& diff_input,
                         const std::vector<AnnotatedHunk>& hunks,
                         ColumnViewState& config,
                         const diffy::ProgramOptions& options,
                         int64_t width);

void
column_view_diff_render(const DiffInput<diffy::Line>& diff_input,
                        const std::vector<AnnotatedHunk>& hunks,
                        ColumnViewState& config,
                        const diffy::ProgramOptions& options);

}  // namespace diffy
