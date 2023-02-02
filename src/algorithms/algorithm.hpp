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

struct Coordinate {
    int64_t x;
    int64_t y;
};

struct Move {
    Coordinate from;
    Coordinate to;
};

enum class EditType {
    Delete,
    Insert,
    Common,
    Meta,
};

struct EditIndex {
    bool valid;
    int64_t value;
    EditIndex() : valid(false), value(0) {
    }

    EditIndex(int64_t in_value) : valid(true), value(in_value) {
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

        auto N = diff_input_.A.size();
        auto M = diff_input_.B.size();

        decltype(N) i = 0;
        if (N == 0 && M > 0) {
            // M insertions
            for (i = 0; i < M; i++) {
                result.edit_sequence.push_back(
                    {EditType::Insert, EditIndexInvalid, EditIndex(static_cast<int64_t>(i))});
            }
            result.status = DiffResultStatus::OK;
            return result;
        } else if (M == 0 && N > 0) {
            // N deletions
            for (i = 0; i < N; i++) {
                result.edit_sequence.push_back(
                    {EditType::Delete, EditIndex(static_cast<int64_t>(i)), EditIndexInvalid});
            }
            result.status = DiffResultStatus::OK;
            return result;
        } else if (N == 0 && M == 0) {
            result.status = DiffResultStatus::OK; // Empty file; no diff.
            return result;
        }

        return diff();
    }
};

}  // namespace diffy