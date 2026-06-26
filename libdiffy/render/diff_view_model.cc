#include "diff_view_model.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <gsl/span>

namespace diffy {

namespace {

// Within a line of a given edit type, a token segment's own type decides whether
// it is the highlighted change or just carried-along text.
SpanStyle
span_style_for(EditType line_type, EditType segment_type) {
    switch (line_type) {
        case EditType::Delete:
            return segment_type == EditType::Delete ? SpanStyle::DeleteToken : SpanStyle::Delete;
        case EditType::Insert:
            return segment_type == EditType::Insert ? SpanStyle::InsertToken : SpanStyle::Insert;
        case EditType::Common:
            return SpanStyle::Common;
        default:
            return SpanStyle::Meta;
    }
}

std::string
strip_eol(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

// Build a content cell from one annotated edit line, pulling the actual text out
// of the source side (A or B). Newlines are dropped: each row is one visual line.
DiffCell
make_cell(const gsl::span<Line>& source, const EditLine& line) {
    DiffCell cell;
    cell.present = true;
    cell.type = line.type;

    const std::string& text = source[static_cast<long>(line.line_index)].line;
    for (const auto& seg : line.segments) {
        std::string piece = strip_eol(text.substr(seg.start, seg.length));
        if (piece.empty()) {
            continue;
        }
        cell.spans.push_back({std::move(piece), span_style_for(line.type, seg.type)});
    }
    return cell;
}

void
build_side_by_side(const DiffInput<Line>& input, const AnnotatedHunk& hunk, std::vector<DiffRow>& rows) {
    const auto& A = hunk.a_lines;
    const auto& B = hunk.b_lines;
    std::size_t i = 0, j = 0;

    while (i < A.size() || j < B.size()) {
        const bool has_a = i < A.size();
        const bool has_b = j < B.size();
        const EditType at = has_a ? A[i].type : EditType::Meta;
        const EditType bt = has_b ? B[j].type : EditType::Meta;

        DiffRow row;
        if (has_a && has_b && at == EditType::Common && bt == EditType::Common) {
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.right = make_cell(input.B, B[j]);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++i;
            ++j;
        } else if (has_a && has_b && at == EditType::Delete && bt == EditType::Insert) {
            // A changed line: show the deletion and insertion as a pair.
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.right = make_cell(input.B, B[j]);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++i;
            ++j;
        } else if (has_a && at == EditType::Delete) {
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            ++i;
        } else if (has_b && bt == EditType::Insert) {
            row.right = make_cell(input.B, B[j]);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++j;
        } else if (has_a) {
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            ++i;
        } else {
            row.right = make_cell(input.B, B[j]);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++j;
        }
        rows.push_back(std::move(row));
    }
}

void
build_unified(const DiffInput<Line>& input, const AnnotatedHunk& hunk, std::vector<DiffRow>& rows) {
    const auto& A = hunk.a_lines;
    const auto& B = hunk.b_lines;
    std::size_t i = 0, j = 0;

    while (i < A.size() || j < B.size()) {
        const bool has_a = i < A.size();
        const bool has_b = j < B.size();
        const EditType at = has_a ? A[i].type : EditType::Meta;
        const EditType bt = has_b ? B[j].type : EditType::Meta;

        DiffRow row;  // right stays !present in unified mode
        if (has_a && has_b && at == EditType::Common && bt == EditType::Common) {
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++i;
            ++j;
        } else if (has_a && at == EditType::Delete) {
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            ++i;
        } else if (has_b && bt == EditType::Insert) {
            row.left = make_cell(input.B, B[j]);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++j;
        } else if (has_a) {
            row.left = make_cell(input.A, A[i]);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            ++i;
        } else {
            row.left = make_cell(input.B, B[j]);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++j;
        }
        rows.push_back(std::move(row));
    }
}

}  // namespace

DiffViewModel
build_diff_view(const DiffInput<Line>& input,
                const std::vector<AnnotatedHunk>& hunks,
                const DiffLayoutOptions& options) {
    DiffViewModel model;
    model.mode = options.mode;

    for (const auto& hunk : hunks) {
        DiffRow header;
        header.kind = RowKind::HunkHeader;
        header.header_text = fmt::format("@@ -{},{} +{},{} @@", hunk.from_start + 1, hunk.from_count,
                                         hunk.to_start + 1, hunk.to_count);
        model.rows.push_back(std::move(header));

        if (options.mode == ViewMode::SideBySide) {
            build_side_by_side(input, hunk, model.rows);
        } else {
            build_unified(input, hunk, model.rows);
        }
    }

    return model;
}

}  // namespace diffy
