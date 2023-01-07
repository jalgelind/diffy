#pragma once

#include "algorithms/algorithm.hpp"
#include "processing/diff_hunk.hpp"
#include "util/readlines.hpp"

namespace diffy {

std::vector<std::string>
get_unified_diff(const DiffInput<Line>& diff_input, const std::vector<Hunk>& hunks);

}  // namespace diffy
