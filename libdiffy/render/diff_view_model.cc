#include "diff_view_model.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <functional>
#include <gsl/span>
#include <unordered_map>

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

// Emit spans for one diff segment [start, start+length) of `text`, split at the
// boundaries of the line's syntax-highlight runs so each emitted span carries a
// single (diff style, syntax group) pair. `runs` is null when the line has no
// highlighting, in which case the whole segment becomes one span (group None).
void
emit_segment_spans(DiffCell& cell,
                   const std::string& text,
                   uint32_t start,
                   uint32_t length,
                   SpanStyle style,
                   const std::vector<HighlightRun>* runs) {
    const uint32_t seg_end = start + length;
    auto push = [&](uint32_t a, uint32_t b, HighlightGroup g) {
        if (b <= a) {
            return;
        }
        std::string piece = strip_eol(text.substr(a, b - a));
        if (!piece.empty()) {
            cell.spans.push_back({std::move(piece), style, g});
        }
    };

    if (!runs) {
        push(start, seg_end, HighlightGroup::None);
        return;
    }

    uint32_t pos = start;
    for (const auto& r : *runs) {
        if (r.end <= start) {
            continue;
        }
        if (r.start >= seg_end) {
            break;
        }
        const uint32_t rs = r.start > start ? r.start : start;
        const uint32_t re = r.end < seg_end ? r.end : seg_end;
        if (rs > pos) {
            push(pos, rs, HighlightGroup::None);  // gap between runs
        }
        push(rs, re, r.group);
        pos = re;
    }
    if (pos < seg_end) {
        push(pos, seg_end, HighlightGroup::None);
    }
}

// Build a content cell from one annotated edit line, pulling the actual text out
// of the source side (A or B). Newlines are dropped: each row is one visual line.
DiffCell
make_cell(const gsl::span<Line>& source, const EditLine& line, const LineHighlights* highlights) {
    DiffCell cell;
    cell.present = true;
    cell.type = line.type;

    const std::string& text = source[static_cast<long>(line.line_index)].line;
    const std::vector<HighlightRun>* runs = nullptr;
    if (highlights && line.line_index < highlights->size() &&
        !(*highlights)[line.line_index].empty()) {
        runs = &(*highlights)[line.line_index];
    }
    for (const auto& seg : line.segments) {
        emit_segment_spans(cell, text, seg.start, seg.length, span_style_for(line.type, seg.type),
                           runs);
    }
    return cell;
}

void
build_side_by_side(const DiffInput<Line>& input, const AnnotatedHunk& hunk, std::vector<DiffRow>& rows,
                   const LineHighlights* a_hl, const LineHighlights* b_hl) {
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
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.right = make_cell(input.B, B[j], b_hl);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++i;
            ++j;
        } else if (has_a && has_b && at == EditType::Delete && bt == EditType::Insert &&
                   A[i].move_id == 0 && B[j].move_id == 0) {
            // A changed line: show the deletion and insertion as a pair. Only when
            // NEITHER side is a moved line — a DiffRow carries a single move_id, so
            // pairing a moved delete with a moved insert here would silently drop the
            // move markers of one (or both). Moved lines instead fall through to the
            // delete-only / insert-only branches below, which preserve move_id, so a
            // relocated block renders whole on its own side (GAP-9).
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.right = make_cell(input.B, B[j], b_hl);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++i;
            ++j;
        } else if (has_a && at == EditType::Delete) {
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.move_id = A[i].move_id;
            row.move_line = A[i].move_line;
            ++i;
        } else if (has_b && bt == EditType::Insert) {
            row.right = make_cell(input.B, B[j], b_hl);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            row.move_id = B[j].move_id;
            row.move_line = B[j].move_line;
            ++j;
        } else if (has_a) {
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            ++i;
        } else {
            row.right = make_cell(input.B, B[j], b_hl);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++j;
        }
        rows.push_back(std::move(row));
    }
}

void
build_unified(const DiffInput<Line>& input, const AnnotatedHunk& hunk, std::vector<DiffRow>& rows,
              const LineHighlights* a_hl, const LineHighlights* b_hl) {
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
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            ++i;
            ++j;
        } else if (has_a && at == EditType::Delete) {
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            row.move_id = A[i].move_id;
            row.move_line = A[i].move_line;
            ++i;
        } else if (has_b && bt == EditType::Insert) {
            row.left = make_cell(input.B, B[j], b_hl);
            row.new_lineno = static_cast<int64_t>(B[j].line_index) + 1;
            row.move_id = B[j].move_id;
            row.move_line = B[j].move_line;
            ++j;
        } else if (has_a) {
            row.left = make_cell(input.A, A[i], a_hl);
            row.old_lineno = static_cast<int64_t>(A[i].line_index) + 1;
            ++i;
        } else {
            row.left = make_cell(input.B, B[j], b_hl);
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
                const DiffLayoutOptions& options,
                const LineHighlights* a_highlights,
                const LineHighlights* b_highlights) {
    DiffViewModel model;
    model.mode = options.mode;

    // Git-style range, matching the unified and column-view @@ headers exactly:
    // from_start/to_start are already 1-based, and a count of 1 is shown as a
    // single number.
    auto fmt_change = [](int64_t start, int64_t count) {
        return count == 1 ? fmt::format("{}", start) : fmt::format("{},{}", start, count);
    };
    for (const auto& hunk : hunks) {
        DiffRow header;
        header.kind = RowKind::HunkHeader;
        header.header_text = fmt::format("@@ -{} +{} @@", fmt_change(hunk.from_start, hunk.from_count),
                                         fmt_change(hunk.to_start, hunk.to_count));
        if (!hunk.context.empty()) {
            header.header_text += " " + hunk.context;
        }
        model.rows.push_back(std::move(header));

        if (options.mode == ViewMode::SideBySide) {
            build_side_by_side(input, hunk, model.rows, a_highlights, b_highlights);
        } else {
            build_unified(input, hunk, model.rows, a_highlights, b_highlights);
        }
    }

    return model;
}

void
detect_cross_file_moves(const std::vector<CrossFileDiff>& files) {
    struct XRef {
        DiffRow* row;
        size_t file;
        int64_t lineno;
        std::string text;
    };
    auto row_text = [](const DiffRow& r) {
        std::string t;
        const DiffCell& cell = r.left.present ? r.left : r.right;
        for (const auto& s : cell.spans) {
            t += s.text;
        }
        return t;
    };

    std::vector<XRef> xdels, xinss;
    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (!files[fi].model) {
            continue;
        }
        for (auto& row : files[fi].model->rows) {
            if (row.kind != RowKind::Content || row.move_id != 0) {
                continue;
            }
            if (row.old_lineno && !row.new_lineno) {
                xdels.push_back({&row, fi, *row.old_lineno, row_text(row)});
            } else if (row.new_lineno && !row.old_lineno) {
                xinss.push_back({&row, fi, *row.new_lineno, row_text(row)});
            }
        }
    }

    constexpr int kMinMoveLines = 3;
    constexpr size_t kMoveBudget = 1u << 22;
    if (xdels.empty() || xinss.empty() || xdels.size() * xinss.size() > kMoveBudget) {
        return;
    }

    std::hash<std::string> H;
    std::unordered_multimap<size_t, size_t> ins_by_hash;
    for (size_t k = 0; k < xinss.size(); ++k) {
        ins_by_hash.emplace(H(xinss[k].text), k);
    }
    std::vector<bool> used(xinss.size(), false);
    int mid = 1'000'000;  // cross-file move ids live in their own range
    for (size_t di = 0; di < xdels.size();) {
        size_t best_len = 0, best_ii = 0;
        auto range = ins_by_hash.equal_range(H(xdels[di].text));
        for (auto it = range.first; it != range.second; ++it) {
            size_t ii = it->second;
            if (used[ii] || xinss[ii].file == xdels[di].file) {
                continue;  // cross-FILE only
            }
            size_t len = 0;
            while (di + len < xdels.size() && ii + len < xinss.size() && !used[ii + len] &&
                   xdels[di + len].file == xdels[di].file && xinss[ii + len].file == xinss[ii].file &&
                   xdels[di + len].text == xinss[ii + len].text) {
                ++len;
            }
            if (len > best_len) {
                best_len = len;
                best_ii = ii;
            }
        }
        if (best_len >= static_cast<size_t>(kMinMoveLines)) {
            ++mid;
            // Every row of a block points at the block's START line (matches the
            // within-file detect_moves behaviour), so the whole block reads "→ file:N".
            const int64_t ins_start = xinss[best_ii].lineno;
            const int64_t del_start = xdels[di].lineno;
            const std::string& ins_path = files[xinss[best_ii].file].path;
            const std::string& del_path = files[xdels[di].file].path;
            for (size_t k = 0; k < best_len; ++k) {
                XRef& d = xdels[di + k];
                XRef& s = xinss[best_ii + k];
                d.row->move_id = mid;
                d.row->move_line = ins_start;
                d.row->move_file = ins_path;
                s.row->move_id = mid;
                s.row->move_line = del_start;
                s.row->move_file = del_path;
                used[best_ii + k] = true;
            }
            di += best_len;
        } else {
            ++di;
        }
    }
}

}  // namespace diffy
