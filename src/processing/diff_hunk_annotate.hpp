#pragma once

/*
    Diff hunk re-formatter. Take a closer look at what differs within a hunk, and split the
    lines into segments with annotation information.

    We look at repeated whitespace (to highlight indentation changes and trailing spaces), and repeated
    sequences of any other character.
*/

#include "algorithms/algorithm.hpp"
#include "processing/context_suggestion.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/tokenizer.hpp"
#include "util/readlines.hpp"

#include <string>
#include <vector>

namespace diffy {

struct LineSegment {
    std::size_t start;
    std::size_t length;
    TokenFlag flags;
    EditType type;
};

struct EditLine {
    EditType type;
    EditIndex line_index;
    std::vector<LineSegment> segments;
};

struct AnnotatedHunk {
    int64_t from_start = 0;
    int64_t from_count = 0;
    int64_t to_start = 0;
    int64_t to_count = 0;

    std::vector<EditLine> a_lines;
    std::vector<EditLine> b_lines;

    std::optional<diffy::Suggestion> a_hunk_context;
    std::optional<diffy::Suggestion> b_hunk_context;
};

enum class EditGranularity {
    Line,   // Whole line insert/delete
    Token,  // Words and operators separated by whitespace
};

std::vector<AnnotatedHunk>
annotate_hunks(const DiffInput<Line>& diff_input,
               const std::vector<Hunk>& hunks,
               const EditGranularity granularity,
               bool ignore_whitespace);

}  // namespace diffy