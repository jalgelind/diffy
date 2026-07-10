#include "indent_heuristic.hpp"

#include <cstddef>
#include <gsl/span>
#include <string>

namespace diffy {

namespace {

// git's magic constants (xdiffi.c). Do not tune casually — they encode the
// tested heuristic weights.
constexpr int MAX_INDENT = 200;
constexpr int INDENT_WEIGHT = 60;
constexpr int MAX_BLANKS = 20;
constexpr int START_OF_FILE_PENALTY = 1;
constexpr int END_OF_FILE_PENALTY = 21;
constexpr int TOTAL_BLANK_WEIGHT = -30;
constexpr int POST_BLANK_WEIGHT = 6;
constexpr int RELATIVE_INDENT_PENALTY = -4;
constexpr int RELATIVE_INDENT_WITH_BLANK_PENALTY = 10;
constexpr int RELATIVE_OUTDENT_PENALTY = 24;
constexpr int RELATIVE_OUTDENT_WITH_BLANK_PENALTY = 17;
constexpr int RELATIVE_DEDENT_PENALTY = 23;
constexpr int RELATIVE_DEDENT_WITH_BLANK_PENALTY = 17;
constexpr long INDENT_HEURISTIC_MAX_SLIDING = 100;

// Visual indent width of a line (spaces=1, tab to next multiple of 8), capped at
// MAX_INDENT. Returns -1 for a blank (all-whitespace) line. The trailing newline
// is whitespace and never reached before a non-space, so it doesn't matter.
int
get_indent(const std::string& s) {
    int ret = 0;
    for (char c : s) {
        if (c == ' ') {
            ret += 1;
        } else if (c == '\t') {
            ret += 8 - ret % 8;
        } else if (c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            ret += 1;  // other whitespace: count as one (git ignores; blank check below)
        } else {
            return ret;  // first non-space: this is the indent
        }
        if (ret >= MAX_INDENT) {
            return MAX_INDENT;
        }
    }
    return -1;  // only whitespace
}

struct SplitMeasurement {
    bool end_of_file = false;
    int indent = -1;       // indent at the split line (-1 = blank / EOF)
    int pre_blank = 0;     // blank lines immediately before the split
    int pre_indent = -1;   // indent of the nearest non-blank line before
    int post_blank = 0;    // blank lines immediately after the split
    int post_indent = -1;  // indent of the nearest non-blank line after
};

struct SplitScore {
    int effective_indent = 0;
    int penalty = 0;
};

void
measure_split(const gsl::span<Line>& lines, long split, SplitMeasurement& m) {
    const long n = static_cast<long>(lines.size());
    if (split >= n) {
        m.end_of_file = true;
        m.indent = -1;
    } else {
        m.end_of_file = false;
        m.indent = get_indent(lines[split].line);
    }
    m.pre_blank = 0;
    m.pre_indent = -1;
    for (long i = split - 1; i >= 0; i--) {
        m.pre_indent = get_indent(lines[i].line);
        if (m.pre_indent != -1) {
            break;
        }
        m.pre_blank += 1;
        if (m.pre_blank == MAX_BLANKS) {
            m.pre_indent = 0;
            break;
        }
    }
    m.post_blank = 0;
    m.post_indent = -1;
    for (long i = split + 1; i < n; i++) {
        m.post_indent = get_indent(lines[i].line);
        if (m.post_indent != -1) {
            break;
        }
        m.post_blank += 1;
        if (m.post_blank == MAX_BLANKS) {
            m.post_indent = 0;
            break;
        }
    }
}

void
score_add_split(const SplitMeasurement& m, SplitScore& s) {
    if (m.pre_indent == -1 && m.pre_blank == 0) {
        s.penalty += START_OF_FILE_PENALTY;
    }
    if (m.end_of_file) {
        s.penalty += END_OF_FILE_PENALTY;
    }
    const int post_blank = (m.indent == -1) ? 1 + m.post_blank : 0;
    const int total_blank = m.pre_blank + post_blank;
    const bool any_blanks = (total_blank != 0);
    s.penalty += TOTAL_BLANK_WEIGHT * total_blank;
    s.penalty += POST_BLANK_WEIGHT * post_blank;

    const int indent = (m.indent != -1) ? m.indent : m.post_indent;
    if (indent == -1) {
        return;
    }
    s.effective_indent += INDENT_WEIGHT * indent;

    if (m.pre_indent == -1) {
        return;  // nothing before to compare against
    }
    if (indent > m.pre_indent) {
        s.penalty += any_blanks ? RELATIVE_INDENT_WITH_BLANK_PENALTY : RELATIVE_INDENT_PENALTY;
    } else if (indent == m.pre_indent) {
        // same indent as the previous non-blank line: no penalty.
    } else {  // indent < pre_indent: indentation decreasing
        if (m.post_indent != -1 && m.post_indent > indent) {
            s.penalty += any_blanks ? RELATIVE_OUTDENT_WITH_BLANK_PENALTY : RELATIVE_OUTDENT_PENALTY;
        } else {
            s.penalty += any_blanks ? RELATIVE_DEDENT_WITH_BLANK_PENALTY : RELATIVE_DEDENT_PENALTY;
        }
    }
}

// <= 0 means s1 is at least as good as s2 (prefers the later shift on ties, as git
// does). Lower effective_indent wins; ties broken by lower penalty.
int
score_cmp(const SplitScore& s1, const SplitScore& s2) {
    const int cmp_indents = (s1.effective_indent > s2.effective_indent) -
                            (s1.effective_indent < s2.effective_indent);
    return INDENT_WEIGHT * cmp_indents + (s1.penalty - s2.penalty);
}

// Slide each pure change group (marked in `changed`) within one side's lines to
// the position the indent heuristic scores best.
void
slide_side(const gsl::span<Line>& lines, std::vector<char>& changed) {
    const long n = static_cast<long>(lines.size());
    long g0 = 0;
    while (g0 < n) {
        if (!changed[g0]) {
            ++g0;
            continue;
        }
        long g1 = g0;
        while (g1 < n && changed[g1]) {
            ++g1;
        }
        const long groupsize = g1 - g0;
        const long region_end = g1;  // where to resume scanning after this group

        // Float the group: clear its cells so the slide-range scan below isn't blocked
        // by the group's own marks (only truly-unchanged cells gate a slide).
        for (long i = g0; i < g1; ++i) {
            changed[i] = 0;
        }

        // Slide the group as far up as possible (line leaving the bottom equals the
        // line entering the top): lines[g0-1] == lines[g1-1].
        while (g0 > 0 && changed[g0 - 1] == 0 && lines[g0 - 1].line == lines[g1 - 1].line) {
            --g0;
            --g1;
        }
        const long earliest_end = g1;

        // Bottom-most end: keep sliding down while lines[end - groupsize] == lines[end].
        long end = g1;
        while (end < n && changed[end] == 0 && lines[end - groupsize].line == lines[end].line) {
            ++end;
        }
        const long bottom_end = end;

        // Score every candidate end position; pick the best (git prefers the last on
        // ties). Each position implies a split before and after the group.
        long best_shift = -1;
        SplitScore best_score;
        long shift = earliest_end;
        if (bottom_end - INDENT_HEURISTIC_MAX_SLIDING > shift) {
            shift = bottom_end - INDENT_HEURISTIC_MAX_SLIDING;
        }
        for (; shift <= bottom_end; ++shift) {
            SplitScore score;
            SplitMeasurement m;
            measure_split(lines, shift, m);
            score_add_split(m, score);
            measure_split(lines, shift - groupsize, m);
            score_add_split(m, score);
            if (best_shift == -1 || score_cmp(score, best_score) <= 0) {
                best_score = score;
                best_shift = shift;
            }
        }

        // Reposition: clear the whole slide span, then mark the group at best_shift.
        for (long i = g0; i < bottom_end; ++i) {
            changed[i] = 0;
        }
        for (long i = best_shift - groupsize; i < best_shift; ++i) {
            changed[i] = 1;
        }
        g0 = bottom_end;  // continue past the region we just settled
    }
}

}  // namespace

void
apply_indent_heuristic(const DiffInput<Line>& input, std::vector<Edit>& edit_sequence) {
    const long N = static_cast<long>(input.A.size());
    const long M = static_cast<long>(input.B.size());
    if (N == 0 || M == 0) {
        return;  // pure add/delete of a whole file: nothing to slide against
    }

    // Per-side change flags derived from the edit sequence.
    std::vector<char> a_changed(static_cast<size_t>(N), 0);
    std::vector<char> b_changed(static_cast<size_t>(M), 0);
    for (const auto& e : edit_sequence) {
        if (e.type == EditType::Delete && e.a_index.valid) {
            a_changed[static_cast<size_t>(e.a_index.value)] = 1;
        } else if (e.type == EditType::Insert && e.b_index.valid) {
            b_changed[static_cast<size_t>(e.b_index.value)] = 1;
        }
    }

    slide_side(input.A, a_changed);
    slide_side(input.B, b_changed);

    // Rebuild the sequence from the (possibly shifted) flags. Unchanged A/B lines are
    // the LCS and pair up in order; sliding only ever swaps an unchanged line for one
    // of identical content, so the pairing stays valid. Deletes precede inserts within
    // a change region (the existing convention).
    std::vector<Edit> out;
    out.reserve(edit_sequence.size());
    long i = 0, j = 0;
    while (i < N || j < M) {
        if (i < N && a_changed[static_cast<size_t>(i)]) {
            out.push_back({EditType::Delete, EditIndex(i), EditIndexInvalid});
            ++i;
        } else if (j < M && b_changed[static_cast<size_t>(j)]) {
            out.push_back({EditType::Insert, EditIndexInvalid, EditIndex(j)});
            ++j;
        } else {
            out.push_back({EditType::Common, EditIndex(i), EditIndex(j)});
            ++i;
            ++j;
        }
    }
    edit_sequence = std::move(out);
}

}  // namespace diffy
