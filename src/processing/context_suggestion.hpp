#pragma once

#include <string>
#include <vector>

#include <util/readlines.hpp>

#include <gsl/span>

namespace diffy {

bool
context_find(gsl::span<diffy::Line> lines, int from, std::vector<std::string>& out_suggestions);

}  // namespace diffy