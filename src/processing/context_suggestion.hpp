#pragma once

#include <vector>

#include <util/readlines.hpp>

namespace diffy {

bool
context_find(std::vector<diffy::Line> lines, int from, std::string& out_suggestion);

}  // namespace diffy