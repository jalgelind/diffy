#pragma once

#include "algorithms/algorithm.hpp"
#include "processing/diff_hunk.hpp"
#include "util/readlines.hpp"

#include <string>
#include <vector>

namespace diffy {

// When hunk_contexts is supplied (one entry per hunk; empties ignored), each
// non-empty label is appended to that hunk's "@@ ... @@" header, git-style.
std::vector<std::string>
unified_diff_render(const DiffInput<Line>& diff_input,
                    const std::vector<Hunk>& hunks,
                    const std::vector<std::string>* hunk_contexts = nullptr);

}  // namespace diffy
