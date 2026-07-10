#include "render/hex_view_model.hpp"

#include "output/hex_common.hpp"

#include <algorithm>
#include <string>

#include <fmt/format.h>

namespace diffy {

namespace {

// Append text as a span, merging into the previous span when the style matches
// (keeps the model compact; the frontend coalesces by colour anyway).
void
add_span(std::vector<StyledSpan>& spans, std::string text, SpanStyle style) {
    if (text.empty()) {
        return;
    }
    if (!spans.empty() && spans.back().style == style) {
        spans.back().text += text;
        return;
    }
    spans.push_back(StyledSpan{std::move(text), style, HighlightGroup::None});
}

// A per-byte cell in the side-by-side layout.
struct Cell {
    bool has_a = false;
    bool has_b = false;
    uint8_t a = 0;
    uint8_t b = 0;
    uint64_t a_off = 0;
    uint64_t b_off = 0;
    HexSegKind kind = HexSegKind::Equal;
};

SpanStyle
side_style(HexSegKind kind, bool left) {
    if (kind == HexSegKind::Equal) {
        return SpanStyle::Common;
    }
    return left ? SpanStyle::DeleteToken : SpanStyle::InsertToken;
}

// One side-by-side pane (offset + hex + ascii) as spans.
std::vector<StyledSpan>
pane_spans(const std::vector<Cell>& row, bool left, int bpr, int width) {
    std::vector<StyledSpan> spans;

    std::string offstr(static_cast<size_t>(width), ' ');
    for (const Cell& c : row) {
        if (left ? c.has_a : c.has_b) {
            offstr = hex_offset(left ? c.a_off : c.b_off, width);
            break;
        }
    }
    add_span(spans, offstr + "  ", SpanStyle::Common);

    for (int k = 0; k < bpr; ++k) {
        const bool in_row = k < static_cast<int>(row.size());
        const bool present = in_row && (left ? row[k].has_a : row[k].has_b);
        if (!present) {
            add_span(spans, "   ", SpanStyle::Common);
            continue;
        }
        const Cell& c = row[static_cast<size_t>(k)];
        const uint8_t v = left ? c.a : c.b;
        if (c.kind == HexSegKind::Replace) {
            // Highlight only the nibble(s) that actually differ from the other side,
            // so a single-nibble edit (e.g. 3a -> 3b) doesn't paint the whole byte.
            const uint8_t o = left ? c.b : c.a;  // the paired byte on the other side
            const SpanStyle changed = side_style(c.kind, left);
            add_span(spans, std::string(1, hex_nibble(v >> 4)),
                     ((v >> 4) == (o >> 4)) ? SpanStyle::Common : changed);
            add_span(spans, std::string(1, hex_nibble(v)),
                     ((v & 0x0F) == (o & 0x0F)) ? SpanStyle::Common : changed);
            add_span(spans, " ", SpanStyle::Common);
        } else {
            add_span(spans, hex_byte(v) + " ", side_style(c.kind, left));
        }
    }

    add_span(spans, "|", SpanStyle::Common);
    for (int k = 0; k < bpr; ++k) {
        const bool in_row = k < static_cast<int>(row.size());
        const bool present = in_row && (left ? row[k].has_a : row[k].has_b);
        if (!present) {
            add_span(spans, " ", SpanStyle::Common);
            continue;
        }
        const Cell& c = row[static_cast<size_t>(k)];
        add_span(spans, std::string(1, ascii_char(left ? c.a : c.b)), side_style(c.kind, left));
    }
    add_span(spans, "|", SpanStyle::Common);
    return spans;
}

// One unified row (prefix + offset + hex + ascii) as spans, over one side's bytes.
std::vector<StyledSpan>
unified_row_spans(char prefix, SpanStyle style, const uint8_t* buf, uint64_t offset, size_t count, int bpr,
                  int width) {
    std::vector<StyledSpan> spans;
    add_span(spans, std::string(1, prefix), style);
    add_span(spans, hex_offset(offset, width) + "  ", SpanStyle::Common);
    for (int k = 0; k < bpr; ++k) {
        if (k < static_cast<int>(count)) {
            add_span(spans, hex_byte(buf[offset + k]) + " ", style);
        } else {
            add_span(spans, "   ", SpanStyle::Common);
        }
    }
    add_span(spans, "|", SpanStyle::Common);
    for (int k = 0; k < static_cast<int>(count); ++k) {
        add_span(spans, std::string(1, ascii_char(buf[offset + k])), style);
    }
    add_span(spans, "|", SpanStyle::Common);
    return spans;
}

DiffRow
header_row(uint64_t a_off, uint64_t b_off, int width) {
    DiffRow row;
    row.kind = RowKind::HunkHeader;
    row.header_text = fmt::format("@@ -{} +{} @@", hex_offset(a_off, width), hex_offset(b_off, width));
    return row;
}

DiffRow
content_row(std::vector<StyledSpan> left, std::vector<StyledSpan> right, bool right_present) {
    DiffRow row;
    row.kind = RowKind::Content;
    row.left.spans = std::move(left);
    row.left.present = true;
    row.left.type = EditType::Common;  // per-byte colour comes from span styles
    row.right.spans = std::move(right);
    row.right.present = right_present;
    row.right.type = EditType::Common;
    return row;
}

// -------- side-by-side --------

DiffViewModel
build_side_by_side(gsl::span<const uint8_t> a, gsl::span<const uint8_t> b, const HexAlignment& alignment,
                   int bpr, uint64_t ctx, int width) {
    DiffViewModel model;
    model.mode = ViewMode::SideBySide;

    std::vector<Cell> row;
    auto flush = [&]() {
        if (row.empty()) {
            return;
        }
        model.rows.push_back(content_row(pane_spans(row, true, bpr, width),
                                         pane_spans(row, false, bpr, width), true));
        row.clear();
    };
    auto add_cell = [&](const Cell& c) {
        row.push_back(c);
        // Flush on the left (A) side's bytes-per-row grid boundary so equal
        // regions realign to clean offsets after a length change (like xxd).
        // Cells with no A byte (pure insertions) can't align, so cap at bpr.
        const bool at_grid = c.has_a && (c.a_off % static_cast<uint64_t>(bpr) == static_cast<uint64_t>(bpr) - 1);
        if (at_grid || static_cast<int>(row.size()) == bpr) {
            flush();
        }
    };

    for (size_t si = 0; si < alignment.size(); ++si) {
        const HexSegment& seg = alignment[si];
        if (seg.kind == HexSegKind::Replace) {
            for (uint64_t i = 0; i < seg.a_len; ++i) {
                add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                          seg.b_offset + i, HexSegKind::Replace});
            }
        } else if (seg.kind == HexSegKind::OnlyA) {
            // Insertions/deletions can't share the A grid, so give them their own
            // rows rather than desyncing the surrounding offset columns.
            flush();
            for (uint64_t i = 0; i < seg.a_len; ++i) {
                add_cell({true, false, a[seg.a_offset + i], 0, seg.a_offset + i, 0, HexSegKind::OnlyA});
            }
            flush();
        } else if (seg.kind == HexSegKind::OnlyB) {
            flush();
            for (uint64_t i = 0; i < seg.b_len; ++i) {
                add_cell({false, true, 0, b[seg.b_offset + i], 0, seg.b_offset + i, HexSegKind::OnlyB});
            }
            flush();
        } else {  // Equal
            const std::vector<HexRow> rows = hex_grid_rows(seg.a_offset, seg.a_len, bpr);
            const HexWindow w =
                hex_equal_window(rows.size(), si == 0, si + 1 == alignment.size(), ctx);
            if (w.head == 0 && w.omitted == 0 && w.tail == 0) {
                continue;
            }
            const uint64_t len = seg.a_len;
            const uint64_t head = w.head < rows.size() ? rows[w.head].offset - seg.a_offset : len;
            const uint64_t tail_start =
                w.head + w.omitted < rows.size() ? rows[w.head + w.omitted].offset - seg.a_offset : len;
            for (uint64_t i = 0; i < head; ++i) {
                add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                          seg.b_offset + i, HexSegKind::Equal});
            }
            // No resume marker on the last segment (tail == 0): it would point past
            // EOF with no context following it.
            if (w.omitted > 0 && w.tail > 0) {
                flush();
                model.rows.push_back(header_row(seg.a_offset + tail_start, seg.b_offset + tail_start,
                                                width));
            }
            for (uint64_t i = tail_start; i < len; ++i) {
                add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                          seg.b_offset + i, HexSegKind::Equal});
            }
        }
    }
    flush();
    return model;
}

// -------- unified --------

void
emit_unified_rows(DiffViewModel& model, char prefix, SpanStyle style, const uint8_t* buf,
                  const std::vector<HexRow>& rows, size_t first_row, size_t num_rows, int bpr, int width) {
    for (size_t r = first_row; r < first_row + num_rows && r < rows.size(); ++r) {
        const HexRow& row = rows[r];
        model.rows.push_back(content_row(
            unified_row_spans(prefix, style, buf, row.offset, static_cast<size_t>(row.count), bpr, width),
            {}, false));
    }
}

DiffViewModel
build_unified(gsl::span<const uint8_t> a, gsl::span<const uint8_t> b, const HexAlignment& alignment, int bpr,
              uint64_t ctx, int width) {
    DiffViewModel model;
    model.mode = ViewMode::Unified;

    for (size_t si = 0; si < alignment.size(); ++si) {
        const HexSegment& seg = alignment[si];
        switch (seg.kind) {
            case HexSegKind::Replace:
                emit_unified_rows(model, '-', SpanStyle::DeleteToken, a.data(),
                                  hex_grid_rows(seg.a_offset, seg.a_len, bpr), 0, seg.a_len, bpr, width);
                emit_unified_rows(model, '+', SpanStyle::InsertToken, b.data(),
                                  hex_grid_rows(seg.b_offset, seg.b_len, bpr), 0, seg.b_len, bpr, width);
                break;
            case HexSegKind::OnlyA:
                emit_unified_rows(model, '-', SpanStyle::DeleteToken, a.data(),
                                  hex_grid_rows(seg.a_offset, seg.a_len, bpr), 0, seg.a_len, bpr, width);
                break;
            case HexSegKind::OnlyB:
                emit_unified_rows(model, '+', SpanStyle::InsertToken, b.data(),
                                  hex_grid_rows(seg.b_offset, seg.b_len, bpr), 0, seg.b_len, bpr, width);
                break;
            case HexSegKind::Equal: {
                const std::vector<HexRow> rows = hex_grid_rows(seg.a_offset, seg.a_len, bpr);
                const HexWindow w =
                    hex_equal_window(rows.size(), si == 0, si + 1 == alignment.size(), ctx);
                if (w.head == 0 && w.omitted == 0 && w.tail == 0) {
                    break;
                }
                if (w.head > 0) {
                    emit_unified_rows(model, ' ', SpanStyle::Common, a.data(), rows, 0, w.head, bpr, width);
                }
                // Last segment has tail == 0, so head + omitted == rows.size(): a
                // resume marker there would index out of bounds and point past EOF.
                if (w.omitted > 0 && w.tail > 0) {
                    const uint64_t resume = rows[w.head + w.omitted].offset;
                    model.rows.push_back(
                        header_row(resume, seg.b_offset + (resume - seg.a_offset), width));
                }
                if (w.tail > 0) {
                    emit_unified_rows(model, ' ', SpanStyle::Common, a.data(), rows, w.head + w.omitted,
                                      w.tail, bpr, width);
                }
                break;
            }
        }
    }
    return model;
}

}  // namespace

DiffViewModel
build_hex_view(gsl::span<const uint8_t> a, gsl::span<const uint8_t> b, const HexAlignment& alignment,
               const DiffLayoutOptions& options, int bytes_per_row, int64_t context_rows) {
    const int bpr = bytes_per_row > 0 ? bytes_per_row : 16;
    const int width = hex_offset_width(std::max<uint64_t>(a.size(), b.size()));
    const uint64_t ctx = static_cast<uint64_t>(context_rows < 0 ? 0 : context_rows);

    if (options.mode == ViewMode::Unified) {
        return build_unified(a, b, alignment, bpr, ctx, width);
    }
    return build_side_by_side(a, b, alignment, bpr, ctx, width);
}

}  // namespace diffy
