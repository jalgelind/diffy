#pragma once

#include "algorithms/algorithm.hpp"
#include "processing/diff_hunk.hpp"
#include "util/readlines.hpp"

#include <functional>
#include <string_view>

namespace diffy {

bool
unified_diff_render(const DiffInput<Line>& diff_input,
                    const HunkStream& hunks,
                    const std::function<void(std::string_view)>& emit_line);

}  // namespace diffy
