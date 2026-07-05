#pragma once

// Byte-level global sequence alignment (Needleman-Wunsch) with unit costs.
//
// Used by the binary/hex diff to align two byte regions so that a small
// insertion opens a gap instead of shifting every following byte into "changed".
// This is O(na*nb) time and space, so callers must only hand it BOUNDED regions
// (see hex_align's fine/global caps). For whole large files, the chunk-level
// pass re-synchronises first and this only refines the small changed windows.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace diffy {

// Per-position alignment op, left (A) to right (B):
//   Equal    - one byte on each side, equal
//   Replace  - one byte on each side, differing
//   DeleteA  - one byte present only in A (a gap on the B side)
//   InsertB  - one byte present only in B (a gap on the A side)
enum class AlignOp : uint8_t { Equal, Replace, DeleteA, InsertB };

inline std::vector<AlignOp>
needleman_wunsch_bytes(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
    std::vector<AlignOp> ops;

    if (na == 0 && nb == 0) {
        return ops;
    }
    if (na == 0) {
        ops.assign(nb, AlignOp::InsertB);
        return ops;
    }
    if (nb == 0) {
        ops.assign(na, AlignOp::DeleteA);
        return ops;
    }

    // cost[i*(nb+1)+j] = edit distance between a[0..i) and b[0..j).
    const size_t stride = nb + 1;
    std::vector<int32_t> cost((na + 1) * stride);

    for (size_t j = 0; j <= nb; ++j) {
        cost[j] = static_cast<int32_t>(j);
    }
    for (size_t i = 1; i <= na; ++i) {
        cost[i * stride] = static_cast<int32_t>(i);
        const uint8_t ai = a[i - 1];
        for (size_t j = 1; j <= nb; ++j) {
            const int32_t sub = cost[(i - 1) * stride + (j - 1)] + (ai == b[j - 1] ? 0 : 1);
            const int32_t del = cost[(i - 1) * stride + j] + 1;
            const int32_t ins = cost[i * stride + (j - 1)] + 1;
            int32_t best = sub;
            if (del < best) best = del;
            if (ins < best) best = ins;
            cost[i * stride + j] = best;
        }
    }

    // Traceback from (na, nb) to (0, 0). Prefer the diagonal on ties so runs of
    // aligned bytes stay together rather than fragmenting into gaps.
    size_t i = na, j = nb;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const int32_t here = cost[i * stride + j];
            const bool equal = a[i - 1] == b[j - 1];
            const int32_t diag = cost[(i - 1) * stride + (j - 1)];
            if (here == diag + (equal ? 0 : 1)) {
                ops.push_back(equal ? AlignOp::Equal : AlignOp::Replace);
                --i;
                --j;
                continue;
            }
            if (here == cost[(i - 1) * stride + j] + 1) {
                ops.push_back(AlignOp::DeleteA);
                --i;
                continue;
            }
            ops.push_back(AlignOp::InsertB);
            --j;
        } else if (i > 0) {
            ops.push_back(AlignOp::DeleteA);
            --i;
        } else {
            ops.push_back(AlignOp::InsertB);
            --j;
        }
    }

    std::reverse(ops.begin(), ops.end());
    return ops;
}

}  // namespace diffy
