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
    std::vector<HunkRange> context_ranges = hunk_ranges;
    for (size_t i = 0; i < context_ranges.size(); i++) {
        const size_t context_ranges_index_max = context_ranges.size() - 1;
        const bool last_iteration = i == context_ranges_index_max;

        HunkRange* p = i == 0 ? 0 : &context_ranges[i - 1];
        HunkRange* h = &context_ranges[i];
        HunkRange* n = last_iteration ? 0 : &context_ranges[i + 1];

        // Combine hunks if they are separated by at most ´context_size´ number
        // of common lines. Consider that there are context lines at the end
        // of this chunk and at the start of the next one.
        if (n && n->start - h->end < context_size * 2 + 2) {
            context_ranges[i] = {h->start, n->end};
            auto erase_pos = context_ranges.begin() + static_cast<int>(i) + 1;
            context_ranges.erase(erase_pos);
            i -= 1;
            continue;
        }

// TODO: When two hunk ranges are separated by empty common lines
//       (for files they would be line breaks), we might want to join
//       them.
#if 0
        if (n) {
            int delta = n->start - h->end;
            if (delta == 2 && edit_sequence[n->start - 1].type == EditType::Common) {
                context_ranges[i] = {h->start, n->end};
                context_ranges.erase(context_ranges.begin()+i+1);
                i -= 1;
                continue;
            }
        }
#endif

        // Adjust context lines for current hunk. Handle edge cases for a
        // first hunk at the start, and a final hunk at the end.
        {
            const int64_t p_end = p ? p->end : 0;
            h->start = std::max(p_end, h->start - context_size);

            if (n) {
                h->end = std::min(n->start, h->end + context_size);
            } else {
                // Don't extend past valid boundaries
                h->end = std::min(h->end + context_size, static_cast<int64_t>(edit_sequence.size()) - 1);
            }
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
