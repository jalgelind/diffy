#pragma once

#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstddef>
#include <gsl/span>
#include <string>
#include <vector>

namespace diffy {

using std::int64_t;
using std::size_t;

// Which line-diff algorithm to run. Lives here (not in config) so the render
// pipeline can name it without pulling in the terminal/theme config types.
enum class Algo { kInvalid, kMyersGreedy, kMyersLinear, kPatience };

Algo
algo_from_string(std::string s);

struct Coordinate {
    int64_t x;
    int64_t y;
};

struct Move {
    Coordinate from;
    Coordinate to;
};

enum class EditType : uint8_t {
    Delete,
    Insert,
    Common,
    Meta,
};

// 32-bit value keeps Edit small; >2e9 lines isn't a realistic input.
struct EditIndex {
    bool valid;
    int32_t value;
    EditIndex() : valid(false), value(0) {
    }

    EditIndex(int64_t in_value) : valid(true), value(static_cast<int32_t>(in_value)) {
    }

    operator int64_t() const {
        return value;
    }
};

const EditIndex EditIndexInvalid{};

// An Edit is part of an edit sequence for turning A into B.
// @cleanup doc
struct Edit {
    EditType type;

    EditIndex a_index;
    EditIndex b_index;
};

enum class DiffResultStatus {
    OK,
    Failed,
    NoChanges,
};

template <typename Unit>
struct DiffInput {
    gsl::span<Unit> A;
    gsl::span<Unit> B;

    std::string A_name;
    std::string B_name;
};

struct DiffResult {
    DiffResultStatus status;
    std::vector<Edit> edit_sequence;
};

template <typename Unit>
class Algorithm {
   public:
    DiffInput<Unit>& diff_input_;

    Algorithm(DiffInput<Unit>& diff_input) : diff_input_(diff_input) {
    }

    virtual ~Algorithm() = default;

    virtual DiffResult
    diff() = 0;

    DiffResult
    compute() {
        DiffResult result;
        result.status = DiffResultStatus::Failed;

        const int64_t N = static_cast<int64_t>(diff_input_.A.size());
        const int64_t M = static_cast<int64_t>(diff_input_.B.size());

        if (N == 0 && M > 0) {
            for (int64_t i = 0; i < M; i++) {
                result.edit_sequence.push_back({EditType::Insert, EditIndexInvalid, EditIndex(i)});
            }
            result.status = DiffResultStatus::OK;
            return result;
        } else if (M == 0 && N > 0) {
            for (int64_t i = 0; i < N; i++) {
                result.edit_sequence.push_back({EditType::Delete, EditIndex(i), EditIndexInvalid});
            }
            result.status = DiffResultStatus::OK;
            return result;
        } else if (N == 0 && M == 0) {
            result.status = DiffResultStatus::OK;  // Empty file; no diff.
            return result;
        }

        // Common prefix/suffix trim: a shared leading/trailing run is Common in
        // every algorithm, so peel it off once here and run the concrete diff() on
        // just the differing core. Big cheap win for the GUI's re-diff-on-toggle.
        // Uses Unit::operator== (content-verified for Line, so it honours
        // ignore_whitespace and guards hash collisions).
        auto& A = diff_input_.A;
        auto& B = diff_input_.B;
        int64_t prefix = 0;
        while (prefix < N && prefix < M && A[prefix] == B[prefix]) {
            ++prefix;
        }
        int64_t suffix = 0;
        while (suffix < N - prefix && suffix < M - prefix && A[N - 1 - suffix] == B[M - 1 - suffix]) {
            ++suffix;
        }
        if (prefix == 0 && suffix == 0) {
            return diff();  // nothing shared to peel; run the algorithm as-is
        }

        for (int64_t i = 0; i < prefix; ++i) {
            result.edit_sequence.push_back({EditType::Common, EditIndex(i), EditIndex(i)});
        }

        const int64_t core_n = N - prefix - suffix;
        const int64_t core_m = M - prefix - suffix;
        if (core_n == 0 && core_m == 0) {
            // Whole input was a shared prefix + suffix: nothing but Common lines.
        } else if (core_n == 0) {
            for (int64_t j = 0; j < core_m; ++j) {
                result.edit_sequence.push_back({EditType::Insert, EditIndexInvalid, EditIndex(prefix + j)});
            }
        } else if (core_m == 0) {
            for (int64_t i = 0; i < core_n; ++i) {
                result.edit_sequence.push_back({EditType::Delete, EditIndex(prefix + i), EditIndexInvalid});
            }
        } else {
            // Diff only the core, then shift its indices back to absolute A/B.
            const gsl::span<Unit> saved_A = A;
            const gsl::span<Unit> saved_B = B;
            A = A.subspan(static_cast<size_t>(prefix), static_cast<size_t>(core_n));
            B = B.subspan(static_cast<size_t>(prefix), static_cast<size_t>(core_m));
            DiffResult core = diff();
            A = saved_A;
            B = saved_B;
            if (core.status == DiffResultStatus::Failed) {
                return core;
            }
            for (Edit e : core.edit_sequence) {
                if (e.a_index.valid) {
                    e.a_index.value += static_cast<int32_t>(prefix);
                }
                if (e.b_index.valid) {
                    e.b_index.value += static_cast<int32_t>(prefix);
                }
                result.edit_sequence.push_back(e);
            }
        }

        for (int64_t i = 0; i < suffix; ++i) {
            result.edit_sequence.push_back(
                {EditType::Common, EditIndex(N - suffix + i), EditIndex(M - suffix + i)});
        }

        // Peeling only removes equal ends, so a non-empty core still had real
        // changes; an empty core means the two inputs were identical.
        result.status =
            (core_n == 0 && core_m == 0) ? DiffResultStatus::NoChanges : DiffResultStatus::OK;
        return result;
    }
};

}  // namespace diffy