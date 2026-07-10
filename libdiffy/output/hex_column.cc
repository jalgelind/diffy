#include "output/hex_column.hpp"

#include "output/hex_common.hpp"

#include <algorithm>

#include <fmt/format.h>

using namespace diffy;

namespace {

struct Cell {
    bool has_a = false;
    bool has_b = false;
    uint8_t a = 0;
    uint8_t b = 0;
    uint64_t a_off = 0;
    uint64_t b_off = 0;
    HexSegKind kind = HexSegKind::Equal;
};

struct Styles {
    bool color = false;
    std::string reset;
    std::string header;
    std::string del_base, del_fg;  // left, removed
    std::string add_base, add_fg;  // right, added
    std::string ctx_base;
};

// Build one pane (left = A side, right = B side) for a row of cells.
std::string
build_pane(const std::vector<Cell>& row, bool left, int bpr, int width, const Styles& s) {
    std::string offstr(static_cast<size_t>(width), ' ');
    bool have_off = false;
    for (const Cell& c : row) {
        const bool present = left ? c.has_a : c.has_b;
        if (present) {
            offstr = hex_offset(left ? c.a_off : c.b_off, width);
            have_off = true;
            break;
        }
    }
    (void)have_off;

    std::string hex;
    std::string ascii;
    for (int k = 0; k < bpr; ++k) {
        const bool in_row = k < static_cast<int>(row.size());
        if (!in_row) {
            hex += "   ";
            ascii += ' ';
            continue;
        }
        const Cell& c = row[static_cast<size_t>(k)];
        const bool present = left ? c.has_a : c.has_b;
        if (!present) {
            hex += "   ";
            ascii += ' ';
            continue;
        }
        const uint8_t byte = left ? c.a : c.b;
        const bool changed = c.kind != HexSegKind::Equal;
        if (s.color && changed) {
            const std::string& base = left ? s.del_base : s.add_base;
            const std::string& fg = left ? s.del_fg : s.add_fg;
            hex += base + fg + hex_byte(byte) + " " + s.reset;
            ascii += base + fg + std::string(1, ascii_char(byte)) + s.reset;
        } else {
            hex += hex_byte(byte);
            hex += ' ';
            ascii += ascii_char(byte);
        }
    }
    return offstr + "  " + hex + "|" + ascii + "|";
}

}  // namespace

std::vector<std::string>
diffy::hex_column_render(gsl::span<const uint8_t> a, gsl::span<const uint8_t> b,
                        const HexAlignment& alignment, const ColumnViewTextStyleEscapeCodes* style,
                        int bytes_per_row, int64_t context_rows, int64_t width) {
    std::vector<std::string> out;
    const int off_w = hex_offset_width(std::max<uint64_t>(a.size(), b.size()));

    // Each pane is off_w + 2 (gap) + 3*bpr (hex) + 2 (|ascii|) + bpr (ascii).
    // Two panes plus a 3-char separator, so every extra byte costs 8 columns.
    const int64_t fixed = 2 * (off_w + 4) + 3;  // per-pane fixed cols + separator
    const int64_t per_byte = 8;                 // 3 hex + 1 ascii, both panes
    const int64_t fit = width > 0 ? (width - fixed) / per_byte : 0;

    int bpr;
    if (bytes_per_row > 0) {
        // Explicit request: honour it, but still shrink to fit a narrow terminal.
        bpr = bytes_per_row;
        if (fit >= 1 && fit < bpr) {
            bpr = static_cast<int>(fit);
        }
    } else if (fit >= 8) {
        // Auto: largest multiple of 8 that fits.
        bpr = static_cast<int>((fit / 8) * 8);
    } else {
        // No usable width (no tty): a sane fixed default.
        bpr = width > 0 ? std::max<int>(1, static_cast<int>(fit)) : 16;
    }

    Styles s;
    s.color = style != nullptr;
    if (s.color) {
        s.reset = "\033[0m";
        s.header = style->header;
        s.del_base = style->delete_line;
        s.del_fg = style->delete_token;
        s.add_base = style->insert_line;
        s.add_fg = style->insert_token;
        s.ctx_base = style->common_line;
    }
    const std::string sep = " │ ";  // " │ "

    std::vector<Cell> row;
    auto flush_row = [&]() {
        if (row.empty()) {
            return;
        }
        out.push_back(build_pane(row, true, bpr, off_w, s) + sep + build_pane(row, false, bpr, off_w, s));
        row.clear();
    };
    auto add_cell = [&](const Cell& c) {
        row.push_back(c);
        // Flush on the left (A) side's bytes-per-row grid boundary so equal
        // regions realign to clean offsets after a length change (like xxd),
        // instead of carrying the change's phase forever. Cells with no A byte
        // (pure insertions) can't align to A, so cap their width at bpr.
        const bool at_grid = c.has_a && (c.a_off % static_cast<uint64_t>(bpr) == static_cast<uint64_t>(bpr) - 1);
        if (at_grid || static_cast<int>(row.size()) == bpr) {
            flush_row();
        }
    };
    auto marker = [&](uint64_t a_off, uint64_t b_off) {
        const std::string text =
            fmt::format("@@ -{} +{} @@", hex_offset(a_off, off_w), hex_offset(b_off, off_w));
        out.push_back(s.color && !s.header.empty() ? s.header + text + s.reset : text);
    };

    const uint64_t ctx = static_cast<uint64_t>(context_rows < 0 ? 0 : context_rows);

    for (size_t si = 0; si < alignment.size(); ++si) {
        const HexSegment& seg = alignment[si];
        switch (seg.kind) {
            case HexSegKind::Replace:
                for (uint64_t i = 0; i < seg.a_len; ++i) {
                    add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                              seg.b_offset + i, HexSegKind::Replace});
                }
                break;
            case HexSegKind::OnlyA:
                // Insertions/deletions can't share the A grid, so give them their
                // own rows rather than desyncing the surrounding offset columns.
                flush_row();
                for (uint64_t i = 0; i < seg.a_len; ++i) {
                    add_cell({true, false, a[seg.a_offset + i], 0, seg.a_offset + i, 0, HexSegKind::OnlyA});
                }
                flush_row();
                break;
            case HexSegKind::OnlyB:
                flush_row();
                for (uint64_t i = 0; i < seg.b_len; ++i) {
                    add_cell({false, true, 0, b[seg.b_offset + i], 0, seg.b_offset + i, HexSegKind::OnlyB});
                }
                flush_row();
                break;
            case HexSegKind::Equal: {
                const std::vector<HexRow> rows = hex_grid_rows(seg.a_offset, seg.a_len, bpr);
                const HexWindow w =
                    hex_equal_window(rows.size(), si == 0, si + 1 == alignment.size(), ctx);
                if (w.head == 0 && w.omitted == 0 && w.tail == 0) {
                    break;
                }
                const uint64_t len = seg.a_len;
                const uint64_t head = w.head < rows.size() ? rows[w.head].offset - seg.a_offset : len;
                const uint64_t tail_start =
                    w.head + w.omitted < rows.size() ? rows[w.head + w.omitted].offset - seg.a_offset : len;
                for (uint64_t i = 0; i < head; ++i) {
                    add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                              seg.b_offset + i, HexSegKind::Equal});
                }
                // Skip the resume marker on the last segment (tail == 0): it would
                // point past EOF with no context following it.
                if (w.omitted > 0 && w.tail > 0) {
                    flush_row();
                    marker(seg.a_offset + tail_start, seg.b_offset + tail_start);
                }
                for (uint64_t i = tail_start; i < len; ++i) {
                    add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                              seg.b_offset + i, HexSegKind::Equal});
                }
                break;
            }
        }
    }
    flush_row();

    return out;
}
