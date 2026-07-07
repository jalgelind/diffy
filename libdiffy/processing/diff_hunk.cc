#include "diff_hunk.hpp"

using namespace diffy;

namespace {

struct HunkRange {
    int64_t start;
    int64_t end;
};

// Take a sequence of edits and filter out all common lines.
// Return a list of all ranges with consecutive delete and insertions.
std::vector<HunkRange>
find_hunk_ranges(const std::vector<Edit>& edit_sequence) {
    std::vector<HunkRange> hunk_ranges;
    hunk_ranges.push_back({-1, -1});
    for (size_t i = 0; i < edit_sequence.size(); i++) {
        HunkRange& curr = *(hunk_ranges.end() - 1);
        const EditType etype = edit_sequence[i].type;
        const bool in_hunk = curr.start != -1;
        if (!in_hunk && etype != EditType::Common) {
            curr.start = static_cast<int>(i);
        } else if (in_hunk) {
            curr.end = static_cast<int>(i);
            if (etype == EditType::Common) {
                curr.end -= 1;
                hunk_ranges.push_back({-1, -1});
            }
        }
    }

    HunkRange& last = *(hunk_ranges.end() - 1);
    if (last.start != -1 && last.end == -1) {
        last.end = static_cast<int>(edit_sequence.size() - 1);
    } else if (last.start == -1) {
        hunk_ranges.pop_back();
    }

    return hunk_ranges;
}

// Combine adjacent hunk ranges. Take number of context lines into consideration.
std::vector<HunkRange>
extend_hunk_ranges(const std::vector<Edit>& edit_sequence,
                   const std::vector<HunkRange>& hunk_ranges,
                   const int64_t context_size) {
    // First pass: combine hunks separated by at most `context_size` common lines
    // on each side (so the two context windows would touch/overlap). A single
    // forward pass into `context_ranges` chains adjacent merges without erasing
    // mid-vector — O(H) instead of O(H^2), and no unsigned-index rewind.
    // Merging only ever extends a range's end, so earlier starts stay valid.
    std::vector<HunkRange> context_ranges;
    context_ranges.reserve(hunk_ranges.size());
    for (const HunkRange& hr : hunk_ranges) {
        if (!context_ranges.empty() && hr.start - context_ranges.back().end < context_size * 2 + 2) {
            context_ranges.back().end = hr.end;
        } else {
            context_ranges.push_back(hr);
        }
    }

    // TODO: When two hunk ranges are separated by empty common lines (blank
    //       lines), we might want to join them too — see ALG-2 (slider/indent
    //       heuristic), which subsumes that idea.

    // Second pass: extend each surviving hunk by `context_size` lines, clamped to
    // the neighbouring hunks and the edit-sequence bounds. Left-to-right, so the
    // previous hunk's end is already extended while the next hunk's start is not
    // (its start is never moved by merging), matching the original semantics.
    for (size_t i = 0; i < context_ranges.size(); i++) {
        const int64_t p_end = i == 0 ? 0 : context_ranges[i - 1].end;
        context_ranges[i].start = std::max(p_end, context_ranges[i].start - context_size);
        if (i + 1 < context_ranges.size()) {
            context_ranges[i].end = std::min(context_ranges[i + 1].start, context_ranges[i].end + context_size);
        } else {
            // Don't extend past valid boundaries.
            context_ranges[i].end =
                std::min(context_ranges[i].end + context_size, static_cast<int64_t>(edit_sequence.size()) - 1);
        }
    }
    return context_ranges;
}

}  // namespace

// Compose a list of Hunks from a sequence of edits.
std::vector<Hunk>
diffy::compose_hunks(const std::vector<Edit>& edit_sequence, const int64_t context_size) {
    // DEBUG("compose_hunks: context lines = {}", context_size);

    // Start by finding all hunks without taking context size into consideration.
    auto hunk_ranges = find_hunk_ranges(edit_sequence);

    // And then extend the ranges to include context lines. Join adjacent hunk ranges.
    std::vector<HunkRange> hunk_ranges_with_context =
        extend_hunk_ranges(edit_sequence, hunk_ranges, context_size);

    // Figure out insertion points for the edit sequence.
    struct InsertionPoint {
        int64_t a_insertion_point = -1;
        int64_t b_insertion_point = -1;
    };
    std::vector<InsertionPoint> insertion_points;
    insertion_points.reserve(edit_sequence.size());
    {
        int a_count = 0, b_count = 0;
        for (auto i = 0u; i < edit_sequence.size(); i++) {
            switch (edit_sequence[i].type) {
                case EditType::Insert:
                    b_count++;
                    break;
                case EditType::Delete:
                    a_count++;
                    break;
                case EditType::Common:
                    a_count++;
                    b_count++;
                    break;
                default:
                    assert(false && "Unexpected type");
                    break;
            }
            insertion_points.push_back({a_count, b_count});
        }
    }

    std::vector<Hunk> hunks;
    for (const auto& hunk_range : hunk_ranges_with_context) {
        size_t range_start = hunk_range.start;
        size_t range_end = hunk_range.end;

        assert(range_start < edit_sequence.size());
        assert(range_end < edit_sequence.size());

        auto a_line_index_start = insertion_points[range_start].a_insertion_point;
        auto b_line_index_start = insertion_points[range_start].b_insertion_point;

        // When a hunk starts with a deletion, we will need to adjust the
        // 'b' start index to the first insertion/common line within the same
        // hunk.
        if (edit_sequence[range_start].type == EditType::Delete) {
            for (auto i = range_start; i <= range_end; i++) {
                auto& u = edit_sequence[i];
                if (u.type != EditType::Delete) {
                    b_line_index_start = insertion_points[i].b_insertion_point;
                    break;
                }
            }
        }

        int64_t a_count = 0;
        int64_t b_count = 0;
        std::vector<Edit> edit_units;
        for (auto i = range_start; i <= range_end; i++) {
            const auto& e = edit_sequence[i];
            switch (e.type) {
                case EditType::Insert:
                    b_count++;
                    break;
                case EditType::Delete:
                    a_count++;
                    break;
                case EditType::Common:
                    a_count++;
                    b_count++;
                    break;
                default:
                    assert(0 && "Unexpected type");
                    break;
            }
            edit_units.push_back(edit_sequence[i]);
        }

        hunks.push_back({a_line_index_start, a_count, b_line_index_start, b_count, edit_units});
    }

    return hunks;
}
