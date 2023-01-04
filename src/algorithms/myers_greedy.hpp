#pragma once

// Greedy version of Myers difference algorithm; O((M+N) D).

#include "algorithm.hpp"
#include "util/bipolar_array.hpp"

#include <gsl/span>
#include <memory>
#include <limits>

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

        // TODO: check limits of IndexSizeType, limits of int64_t as used internally.

        trace.reserve(static_cast<std::size_t>(max));
        BipolarArray<IndexSizeType> v{-max, max};

        v[1] = 0;
        for (int64_t d = 0; d <= max; d++) {
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
                    trace.push_back(v);
                    return d;
                }
            }  // for k
            trace.push_back(v);
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

        if (edit_distance < 0) {
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
        constexpr auto u8_max = std::numeric_limits<uint8_t>::max();
        constexpr auto u16_max = std::numeric_limits<uint16_t>::max();
        constexpr auto u32_max = std::numeric_limits<uint32_t>::max();
        if (N < u8_max && M < u8_max) {
            return diff_impl<uint8_t>();
        } else if (N < u16_max && M < u16_max) {
            return diff_impl<uint16_t>();
        } else if (N < u32_max && M < u32_max) {
            return diff_impl<uint32_t>();
        }
        return diff_impl<int64_t>();  // TODO: Why not unsigned?
    }
};

}  // namespace diffy
