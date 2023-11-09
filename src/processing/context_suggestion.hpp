#pragma once

#include <string>
#include <vector>

#include <util/readlines.hpp>

namespace diffy {

bool
context_find(std::vector<diffy::Line> lines, int from, std::vector<std::string>& out_suggestions);

}  // namespace diffy