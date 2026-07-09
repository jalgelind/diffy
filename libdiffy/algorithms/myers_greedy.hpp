#pragma once

// Greedy version of Myers difference algorithm; O((M+N) D).

#include "algorithm.hpp"
#include "myers_linear.hpp"
#include "util/bipolar_array.hpp"

#include <gsl/span>
#include <limits>
#include <memory>

namespace diffy {

template <typename Unit>
struct MyersGreedy : public Algorithm<Unit> {
   public:
    int64_t N;
    int64_t M;

    const gsl::span<Unit>& A;
    const gsl::span<Unit>& B;

    MyersGreedy(DiffInput<Unit>& diff_input)
        : Algorithm<Unit>(diff_input)
        , N(static_cast<int64_t>(diff_input.A.size()))
        , M(static_cast<int64_t>(diff_input.B.size()))
        , A(diff_input.A)
        , B(diff_input.B) {
    }

    virtual ~MyersGreedy() {
    }

    // Store a snapshot of V for each iteration of D for backtracking
    // the solution.
    template <typename IndexSizeType>
    int64_t
    do_edit_distance(std::vector<BipolarArray<IndexSizeType>>& trace) {
        // Invalid input
        if (N == 0 || M == 0) {
            return -1;
        }

        const int64_t max = N + M;

        // Snapshots cost O(D^2) total; bail to linear-space Myers (-2) if the trace
        // would exceed this budget. Backtrack at level d only reads V[k-1], V[k+1]
        // and V[prev_k] for k in [-d, d] — i.e. indices in [-d-1, d+1] — so each
        // snapshot is sliced to that band (clamped to V's range) instead of the full
        // 2*max+1 slots, which is far less memory/memcpy for small d.
        constexpr std::size_t kMaxTraceBytes = 256ull * 1024 * 1024;
        std::size_t trace_bytes = 0;

        trace.reserve(static_cast<std::size_t>(max));
        BipolarArray<IndexSizeType> v{-max, max};

        // The V band backtrack needs at level d. Storage for index i is at offset
        // max+i, so [lo, hi] is the contiguous run [max+lo, max+hi].
        auto band = [max](int64_t d) -> std::pair<int64_t, int64_t> {
            return {std::max<int64_t>(-(d + 1), -max), std::min<int64_t>(d + 1, max)};
        };
        auto snapshot = [&](int64_t d) {
            const auto [lo, hi] = band(d);
            const std::size_t bytes = static_cast<std::size_t>(hi - lo + 1) * sizeof(IndexSizeType);
            BipolarArray<IndexSizeType> s{lo, hi};
            std::memcpy(s.arr_.get(), &v.arr_.get()[max + lo], bytes);
            trace.push_back(std::move(s));
            trace_bytes += bytes;
        };

        v[1] = 0;
        for (int64_t d = 0; d <= max; d++) {
            const auto [blo, bhi] = band(d);
            if (trace_bytes + static_cast<std::size_t>(bhi - blo + 1) * sizeof(IndexSizeType) >
                kMaxTraceBytes) {
                return -2;
            }
            for (int64_t k = -d; k <= d; k += 2) {
                int64_t x = 0, y = 0;
                // Move down, or right.
                if (k == -d || (k != d && v[k - 1] < v[k + 1])) {
                    x = static_cast<int64_t>(v[k + 1]);
                } else {
                    x = static_cast<int64_t>(v[k - 1]) + 1;
                }
                y = x - k;

                // Move diagonally
                while (x < N && y < M && A[x] == B[y]) {
                    ++x;
                    ++y;
                }

                v[k] = static_cast<IndexSizeType>(x);

                if (x >= N && y >= M) {
                    snapshot(d);
                    return d;
                }
            }  // for k
            snapshot(d);
        }  // for d
        assert(0 && "Failed to figure out edit distance");
        return -1;
    }

    template <typename IndexSizeType>
    void
    do_solve(std::vector<BipolarArray<IndexSizeType>>& trace, std::vector<Move>& solution) {
        // Backtrack through the end-points we've gathered.
        int64_t x = N;
        int64_t y = M;
        for (int64_t d = static_cast<int64_t>(trace.size()); d--;) {
            BipolarArray<IndexSizeType>& v = trace[static_cast<size_t>(d)];

            int64_t k = x - y;
            int64_t prev_k = (k == -d || (k != d && v[k - 1] < v[k + 1])) ? k + 1 : k - 1;
            int64_t prev_x = v[prev_k];
            int64_t prev_y = prev_x - prev_k;

            while (x > prev_x && y > prev_y) {
                solution.push_back({{x - 1, y - 1}, {x, y}});
                x--;
                y--;
            }

            if (d > 0) {
                solution.push_back({{prev_x, prev_y}, {x, y}});
            }

            x = prev_x;
            y = prev_y;
        }
    }

    void
    do_diff(std::vector<Edit>& edit_sequence, std::vector<Move>& solution) {
        // We're traversing the solution backwards. Ideally it should already be
        // reversed.
        std::transform(solution.crbegin(), solution.crend(), std::back_inserter(edit_sequence),
                       [](const Move& move) -> Edit {
                           auto& from = move.from;
                           auto& to = move.to;

                           if (from.x == to.x) {
                               return {EditType::Insert, {}, {from.y}};
                           } else if (to.y == from.y) {
                               return {EditType::Delete, {from.x}, {}};
                           } else {
                               return {EditType::Common, {from.x}, {from.y}};
                           }
                       });
    }

    template <typename IndexSizeType>
    DiffResult
    diff_impl() {
        DiffResult result;

        // OPT: If Algorithm ever becomes re-usable (re-assign DiffInput), trace
        //      should be a member var. Profiler says we spend a lot of time destructing
        //      trace.
        std::vector<BipolarArray<IndexSizeType>> trace;
        int64_t edit_distance = do_edit_distance(trace);

        if (edit_distance == -2) {
            // Trace would exceed the memory budget; fall back to linear-space Myers.
            return MyersLinear<Unit>{this->diff_input_}.compute();
        } else if (edit_distance < 0) {
            result.status = DiffResultStatus::Failed;
            return result;
        } else if (edit_distance == 0) {
            result.status = DiffResultStatus::NoChanges;
            return result;
        }

        std::vector<Move> solution;
        do_solve(trace, solution);
        do_diff(result.edit_sequence, solution);

        result.status = DiffResultStatus::OK;
        return result;
    }

    DiffResult
    diff() {
        // Run diff implementation with smaller data type for faster memcpy operations.
        constexpr auto u8_max = std::numeric_limits<uint8_t>::max() / 2;
        constexpr auto u16_max = std::numeric_limits<uint16_t>::max() / 2;
        constexpr auto u32_max = std::numeric_limits<uint32_t>::max() / 2;
        if (N < u8_max && M < u8_max) {
            return diff_impl<uint8_t>();
        } else if (N < u16_max && M < u16_max) {
            return diff_impl<uint16_t>();
        } else if (N < u32_max && M < u32_max) {
            return diff_impl<uint32_t>();
        }
        return diff_impl<uint64_t>();
    }
};

}  // namespace diffy
