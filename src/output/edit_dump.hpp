#pragma once

#include "algorithms/algorithm.hpp"
#include "util/readlines.hpp"

namespace diffy {

void
edit_dump_diff_render(const DiffInput<Line>& diff_input, const DiffResult& result);

}  // namespace diffy
