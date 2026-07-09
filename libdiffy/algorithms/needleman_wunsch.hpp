#pragma once

// Byte-level global sequence alignment (Needleman-Wunsch) with unit costs.
//
// Used by the binary/hex diff to align two byte regions so that a small
// insertion opens a gap instead of shifting every following byte into "changed".
//
// Two strategies, picked automatically:
//   - Banded DP: only cells within a diagonal band of half-width r are computed
//     (O(max(n,m)*r)). r starts small and doubles whenever the optimal path
//     touches the band edge (meaning a wider band might do better); when the
//     path stays inside the band the result is provably optimal. Substitution-
//     heavy but structurally-aligned regions (the common case after prefix/
//     suffix trimming) stay on the diagonal and finish in a tiny band.
//   - Full DP: fallback once the band would cover the whole matrix, or for tiny
//     inputs where banding isn't worth it. O(n*m).
// The cost cell type is narrowed to uint8/uint16/uint32 by the max possible
// distance (n+m), halving/quartering the DP's memory traffic.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace diffy {

// Per-position alignment op, left (A) to right (B):
//   Equal    - one byte on each side, equal
//   Replace  - one byte on each side, differing
//   DeleteA  - one byte present only in A (a gap on the B side)
//   InsertB  - one byte present only in B (a gap on the A side)
enum class AlignOp : uint8_t { Equal, Replace, DeleteA, InsertB };

namespace nw_detail {

// Full O(na*nb) DP + traceback. Every cell is reachable, so no sentinels.
template <typename Cost>
void
full_dp(const uint8_t* a, size_t na, const uint8_t* b, size_t nb, std::vector<AlignOp>& ops) {
    const size_t stride = nb + 1;
    std::vector<Cost> cost((na + 1) * stride);
    for (size_t j = 0; j <= nb; ++j) {
        cost[j] = static_cast<Cost>(j);
    }
    for (size_t i = 1; i <= na; ++i) {
        cost[i * stride] = static_cast<Cost>(i);
        const uint8_t ai = a[i - 1];
        for (size_t j = 1; j <= nb; ++j) {
            const Cost sub = static_cast<Cost>(cost[(i - 1) * stride + (j - 1)] + (ai == b[j - 1] ? 0 : 1));
            const Cost del = static_cast<Cost>(cost[(i - 1) * stride + j] + 1);
            const Cost ins = static_cast<Cost>(cost[i * stride + (j - 1)] + 1);
            Cost best = sub;
            if (del < best) best = del;
            if (ins < best) best = ins;
            cost[i * stride + j] = best;
        }
    }
    size_t i = na, j = nb;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const Cost here = cost[i * stride + j];
            const bool eq = a[i - 1] == b[j - 1];
            if (here == static_cast<Cost>(cost[(i - 1) * stride + (j - 1)] + (eq ? 0 : 1))) {
                ops.push_back(eq ? AlignOp::Equal : AlignOp::Replace);
                --i;
                --j;
                continue;
            }
            if (here == static_cast<Cost>(cost[(i - 1) * stride + j] + 1)) {
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
}

// Banded DP over the diagonal band k = j - i in [klo, khi] (klo<=0<=... covers
// the start k=0 and the end k=nb-na). `ops` is filled with the best in-band
// alignment. Returns a *heuristic* "looks optimal" signal — true when the
// traceback never rode the band edge — used only to decide whether to widen the
// band. This is not a soundness proof: a substitution-heavy diagonal path never
// touches the edge even when a shift larger than the band radius (periodic /
// rotated data) would be cheaper, so the band can stop early on such inputs. The
// full_dp fallback in solve() (once the band would span the whole matrix) bounds
// the worst case; an exact escape-cost test (widen while achieved cost >=
// 2(r+1)-|dn|) would make it sound but defeats banding on high-cost regions.
template <typename Cost>
bool
banded_dp(const uint8_t* a, size_t na, const uint8_t* b, size_t nb, int klo, int khi,
          std::vector<AlignOp>& ops) {
    const Cost INF = std::numeric_limits<Cost>::max();
    const int W = khi - klo + 1;
    std::vector<Cost> cost((na + 1) * static_cast<size_t>(W), INF);

    auto idx = [&](size_t i, int k) -> size_t { return i * static_cast<size_t>(W) + static_cast<size_t>(k - klo); };
    auto at = [&](long i, int k) -> Cost {
        if (i < 0 || k < klo || k > khi) return INF;
        return cost[idx(static_cast<size_t>(i), k)];
    };

    for (size_t i = 0; i <= na; ++i) {
        for (int k = klo; k <= khi; ++k) {
            const long j = static_cast<long>(i) + k;
            if (j < 0 || j > static_cast<long>(nb)) continue;
            if (i == 0 && j == 0) {
                cost[idx(i, k)] = 0;
                continue;
            }
            Cost best = INF;
            if (i > 0 && j > 0) {  // diagonal (i-1, j-1), same k
                const Cost d = at(static_cast<long>(i) - 1, k);
                if (d != INF) {
                    const Cost v = static_cast<Cost>(d + (a[i - 1] == b[static_cast<size_t>(j) - 1] ? 0 : 1));
                    if (v < best) best = v;
                }
            }
            if (i > 0) {  // up = delete A: (i-1, j), k+1
                const Cost u = at(static_cast<long>(i) - 1, k + 1);
                if (u != INF && static_cast<Cost>(u + 1) < best) best = static_cast<Cost>(u + 1);
            }
            if (j > 0) {  // left = insert B: (i, j-1), k-1
                const Cost l = at(static_cast<long>(i), k - 1);
                if (l != INF && static_cast<Cost>(l + 1) < best) best = static_cast<Cost>(l + 1);
            }
            cost[idx(i, k)] = best;
        }
    }

    bool touched = false;
    long i = static_cast<long>(na), j = static_cast<long>(nb);
    while (i > 0 || j > 0) {
        const int k = static_cast<int>(j - i);
        if (k == klo || k == khi) touched = true;
        if (i > 0 && j > 0) {
            const Cost here = at(i, k);
            const bool eq = a[static_cast<size_t>(i) - 1] == b[static_cast<size_t>(j) - 1];
            const Cost d = at(i - 1, k);
            if (d != INF && here == static_cast<Cost>(d + (eq ? 0 : 1))) {
                ops.push_back(eq ? AlignOp::Equal : AlignOp::Replace);
                --i;
                --j;
                continue;
            }
            const Cost u = at(i - 1, k + 1);
            if (u != INF && here == static_cast<Cost>(u + 1)) {
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
    return !touched;
}

template <typename Cost>
std::vector<AlignOp>
solve(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
    const int dn = static_cast<int>(static_cast<long>(nb) - static_cast<long>(na));
    const size_t maxlen = std::max(na, nb);
    std::vector<AlignOp> ops;
    // Valid diagonals are k = j - i with 0<=i<=na, 0<=j<=nb, i.e. k in [-na, nb];
    // cells outside that range can never lie on a path. Clamp the band to it,
    // because an unclamped band for size-skewed inputs (|dn| large) would request
    // (na+1)*W cells with W ~ |dn| + 2r — tens of GB for e.g. 64 KB vs 100 bytes,
    // even though full_dp for the same region is a few MB. And once the band is as
    // wide as the whole column count (W >= nb+1) it computes no fewer cells than
    // full_dp while spanning every diagonal, so full_dp is both smaller and
    // bounded: fall through to it rather than allocating the giant band.
    const int k_min = -static_cast<int>(na);
    const int k_max = static_cast<int>(nb);
    // Band radius starts just past the forced length difference and doubles.
    for (int r = std::max(16, std::abs(dn) + 16); static_cast<size_t>(r) < maxlen; r *= 2) {
        const int klo = std::max(k_min, std::min(0, dn) - r);
        const int khi = std::min(k_max, std::max(0, dn) + r);
        if (static_cast<long>(khi) - klo + 1 >= static_cast<long>(nb) + 1) {
            break;  // band covers the full width => full_dp is smaller and bounded
        }
        ops.clear();
        if (banded_dp<Cost>(a, na, b, nb, klo, khi, ops)) {
            return ops;  // path stayed inside the band => optimal
        }
    }
    ops.clear();
    full_dp<Cost>(a, na, b, nb, ops);
    return ops;
}

}  // namespace nw_detail

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
    // Narrow the cell type to the smallest that holds the max distance (na+nb).
    const size_t total = na + nb;
    if (total < 255u) {
        return nw_detail::solve<uint8_t>(a, na, b, nb);
    }
    if (total < 65535u) {
        return nw_detail::solve<uint16_t>(a, na, b, nb);
    }
    return nw_detail::solve<uint32_t>(a, na, b, nb);
}

}  // namespace diffy
