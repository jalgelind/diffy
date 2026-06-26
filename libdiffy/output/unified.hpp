#pragma once

#include "algorithms/algorithm.hpp"
#include "processing/diff_hunk.hpp"
#include "util/readlines.hpp"

namespace diffy {

std::vector<std::string>
unified_diff_render(const DiffInput<Line>& diff_input, const std::vector<Hunk>& hunks);

}  // namespace diffy
