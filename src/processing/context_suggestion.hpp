#pragma once

#include <vector>

#include <util/readlines.hpp>

namespace diffy {

struct ContextSuggestion {

};

bool
context_find(std::vector<diffy::Line> lines, int from, ContextSuggestion* out_suggestions);

}  // namespace diffy