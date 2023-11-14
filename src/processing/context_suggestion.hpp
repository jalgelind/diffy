#pragma once

#include <string>
#include <vector>

#include <util/readlines.hpp>

#include <gsl/span>

namespace diffy {

struct Suggestion {
    int line_no;
    std::string text;
};

bool
context_find(gsl::span<diffy::Line> lines, int from, std::vector<Suggestion>& out_suggestions);

}  // namespace diffy