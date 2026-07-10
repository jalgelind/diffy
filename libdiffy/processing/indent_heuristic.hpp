#pragma once

/*
    ALG-2: git's indent heuristic. A group of purely inserted (or purely deleted)
    lines whose boundary lines repeat can be "slid" up or down to several equivalent
    positions — all produce a correct diff, but some read far better (aligning to
    blank lines / block boundaries instead of splitting mid-block). This pass shifts
    each slidable group to the most natural position using git's scoring, operating
    on the edit sequence in place before hunk composition.

    Correctness is never affected: only which of several equivalent representations
    is chosen. Ported from git's xdiffi.c (xdl_change_compact + XDF_INDENT_HEURISTIC).
*/

#include "algorithms/algorithm.hpp"
#include "util/readlines.hpp"

#include <vector>

namespace diffy {

void
apply_indent_heuristic(const DiffInput<Line>& input, std::vector<Edit>& edit_sequence);

}  // namespace diffy
