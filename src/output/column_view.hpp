#pragma once

#include "algorithms/algorithm.hpp"
#include "config/config.hpp"
#include "processing/diff_hunk.hpp"           // TODO: just for Hunk struct. Put that in diffy.hpp?
#include "processing/diff_hunk_annotate.hpp"  // TODO: just for Hunk struct. Put that in diffy.hpp?
#include "util/readlines.hpp"

#include <string>

namespace diffy {

struct ColumnViewState {
    ColumnViewCharacters chars;
    ColumnViewSettings settings;
    ColumnViewTextStyle style;

    // automatically calculated based on terminal width?
    int64_t max_row_length = 0;
    // This is automatically adjusted depending on how many lines we show.
    // TODO: configurable setting where -1 is auto?
    int64_t line_number_digits_count = 4;
};

void
column_view_diff(const DiffInput<diffy::Line>& diff_input,
                  const std::vector<AnnotatedHunk>& hunks,
                  ColumnViewState& config,  // TODO: make const
                  int64_t width);

}  // namespace diffy
