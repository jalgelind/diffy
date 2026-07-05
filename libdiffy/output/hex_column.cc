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
        const Cell& c = in_row ? row[static_cast<size_t>(k)] : Cell{};
        const bool present = in_row && (left ? c.has_a : c.has_b);
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
        if (static_cast<int>(row.size()) == bpr) {
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
                for (uint64_t i = 0; i < seg.a_len; ++i) {
                    add_cell({true, false, a[seg.a_offset + i], 0, seg.a_offset + i, 0, HexSegKind::OnlyA});
                }
                break;
            case HexSegKind::OnlyB:
                for (uint64_t i = 0; i < seg.b_len; ++i) {
                    add_cell({false, true, 0, b[seg.b_offset + i], 0, seg.b_offset + i, HexSegKind::OnlyB});
                }
                break;
            case HexSegKind::Equal: {
                const bool show_head = si != 0;
                const bool show_tail = si + 1 != alignment.size();
                if (!show_head && !show_tail) {
                    break;
                }
                const uint64_t len = seg.a_len;
                const uint64_t ctx_bytes = ctx * static_cast<uint64_t>(bpr);
                uint64_t head = 0, tail = 0;
                if (show_head && show_tail && len <= 2 * ctx_bytes) {
                    head = len;  // small enough: show all
                } else {
                    head = show_head ? std::min<uint64_t>(ctx_bytes, len) : 0;
                    tail = show_tail ? std::min<uint64_t>(ctx_bytes, len - head) : 0;
                }
                const uint64_t omitted = len - head - tail;
                for (uint64_t i = 0; i < head; ++i) {
                    add_cell({true, true, a[seg.a_offset + i], b[seg.b_offset + i], seg.a_offset + i,
                              seg.b_offset + i, HexSegKind::Equal});
                }
                if (omitted > 0) {
                    flush_row();
                    marker(seg.a_offset + head + omitted, seg.b_offset + head + omitted);
                }
                for (uint64_t i = len - tail; i < len; ++i) {
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
