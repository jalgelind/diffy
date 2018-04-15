#pragma once

#include "algorithms/algorithm.hpp"
#include "config/config.hpp"
#include "processing/diff_hunk.hpp"           // TODO: just for Hunk struct. Put that in diffy.hpp?
#include "processing/diff_hunk_annotate.hpp"  // TODO: just for Hunk struct. Put that in diffy.hpp?
#include "util/readlines.hpp"

#include <string>

namespace diffy {

void
side_by_side_diff(const DiffInput<diffy::Line>& diff_input,
                  const std::vector<AnnotatedHunk>& hunks,
                  ColumnViewConfig& config,  // TODO: make const
                  int64_t width);

}  // namespace diffy
