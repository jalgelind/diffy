#pragma once

#include "algorithms/algorithm.hpp"
#include "util/readlines.hpp"

namespace diffy {

void
dump_diff_edits(const DiffInput<Line>& diff_input, const DiffResult& result);

}  // namespace diffy
