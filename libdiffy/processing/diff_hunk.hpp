#pragma once

/*
    Compose diff hunks out of an edit sequence.

    The hunks generated are ideal for generating a unified diff.
*/

#include "algorithms/algorithm.hpp"

namespace diffy {

struct Hunk {
    int64_t from_start = 0;
    int64_t from_count = 0;
    int64_t to_start = 0;
    int64_t to_count = 0;

    std::vector<Edit> edit_units;
};

std::vector<Hunk>
compose_hunks(const std::vector<Edit>& edit_sequence, const int64_t context_size);

}  // namespace diffy
