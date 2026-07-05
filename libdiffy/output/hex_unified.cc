#include "output/hex_unified.hpp"

#include "output/hex_common.hpp"

#include <algorithm>

#include <fmt/format.h>

using namespace diffy;

namespace {

enum class RowKind { Context, Del, Add };

struct Styles {
    bool color = false;
    std::string reset;
    std::string header;
    std::string ctx_base, ctx_fg;
    std::string del_base, del_fg;
    std::string add_base, add_fg;
};

// Plain, uncoloured content of one hex row: "{prefix}{offset}  {hex} |{ascii}|".
std::string
row_text(char prefix, const uint8_t* buf, uint64_t offset, size_t count, int bpr, int width) {
    std::string hex;
    std::string ascii;
    for (int k = 0; k < bpr; ++k) {
        if (k < static_cast<int>(count)) {
            const uint8_t b = buf[offset + k];
            hex += hex_byte(b);
            hex += ' ';
            ascii += ascii_char(b);
        } else {
            hex += "   ";
        }
    }
    return fmt::format("{}{}  {}|{}|", prefix, hex_offset(offset, width), hex, ascii);
}

void
emit_rows(std::vector<std::string>& out, RowKind kind, const uint8_t* buf, uint64_t base_offset,
          uint64_t total_len, uint64_t first_row, uint64_t num_rows, int bpr, int width,
          const Styles& s, int64_t fill_width) {
    const char prefix = kind == RowKind::Del ? '-' : kind == RowKind::Add ? '+' : ' ';
    for (uint64_t r = first_row; r < first_row + num_rows; ++r) {
        const uint64_t start = r * static_cast<uint64_t>(bpr);
        if (start >= total_len) {
            break;
        }
        const size_t count = static_cast<size_t>(std::min<uint64_t>(bpr, total_len - start));
        std::string text = row_text(prefix, buf, base_offset + start, count, bpr, width);
        if (!s.color) {
            out.push_back(text);
            continue;
        }
        const std::string& base = kind == RowKind::Del ? s.del_base
                                  : kind == RowKind::Add ? s.add_base
                                                         : s.ctx_base;
        const std::string& fg = kind == RowKind::Del ? s.del_fg
                                : kind == RowKind::Add ? s.add_fg
                                                       : s.ctx_fg;
        std::string fill;
        if (fill_width > 0 && text.size() < static_cast<size_t>(fill_width)) {
            fill.assign(static_cast<size_t>(fill_width) - text.size(), ' ');
        }
        out.push_back(base + fg + text + fill + s.reset);
    }
}

}  // namespace

std::vector<std::string>
diffy::hex_unified_render(gsl::span<const uint8_t> a, gsl::span<const uint8_t> b,
                         const std::string& a_name, const std::string& b_name,
                         const HexAlignment& alignment, const ColumnViewTextStyleEscapeCodes* style,
                         int bytes_per_row, int64_t context_rows, int64_t fill_width) {
    std::vector<std::string> out;
    const int bpr = bytes_per_row > 0 ? bytes_per_row : 16;
    const int width = hex_offset_width(std::max<uint64_t>(a.size(), b.size()));

    Styles s;
    s.color = style != nullptr;
    if (s.color) {
        s.reset = "\033[0m";
        s.header = style->header;
        s.ctx_base = style->common_line;
        s.ctx_fg = style->common_line_number;  // usually none
        s.del_base = style->delete_line;
        s.del_fg = style->delete_token;
        s.add_base = style->insert_line;
        s.add_fg = style->insert_token;
        // Context bytes should read as plain, not as line-number colour.
        s.ctx_fg.clear();
    }

    auto push_header = [&](const std::string& text) {
        if (s.color && !s.header.empty()) {
            out.push_back(s.header + text + s.reset);
        } else {
            out.push_back(text);
        }
    };

    push_header(fmt::format("--- {}", a_name));
    push_header(fmt::format("+++ {}", b_name));

    const uint64_t ctx = static_cast<uint64_t>(context_rows < 0 ? 0 : context_rows);

    for (size_t si = 0; si < alignment.size(); ++si) {
        const HexSegment& seg = alignment[si];
        switch (seg.kind) {
            case HexSegKind::Replace:
                emit_rows(out, RowKind::Del, a.data(), seg.a_offset, seg.a_len, 0,
                          (seg.a_len + bpr - 1) / bpr, bpr, width, s, fill_width);
                emit_rows(out, RowKind::Add, b.data(), seg.b_offset, seg.b_len, 0,
                          (seg.b_len + bpr - 1) / bpr, bpr, width, s, fill_width);
                break;
            case HexSegKind::OnlyA:
                emit_rows(out, RowKind::Del, a.data(), seg.a_offset, seg.a_len, 0,
                          (seg.a_len + bpr - 1) / bpr, bpr, width, s, fill_width);
                break;
            case HexSegKind::OnlyB:
                emit_rows(out, RowKind::Add, b.data(), seg.b_offset, seg.b_len, 0,
                          (seg.b_len + bpr - 1) / bpr, bpr, width, s, fill_width);
                break;
            case HexSegKind::Equal: {
                const uint64_t total_rows = (seg.a_len + bpr - 1) / bpr;
                const bool show_head = si != 0;
                const bool show_tail = si + 1 != alignment.size();
                if (!show_head && !show_tail) {
                    break;  // whole file equal: nothing to show
                }
                if (show_head && show_tail && total_rows <= 2 * ctx) {
                    emit_rows(out, RowKind::Context, a.data(), seg.a_offset, seg.a_len, 0, total_rows, bpr,
                              width, s, fill_width);
                    break;
                }
                const uint64_t head = show_head ? std::min<uint64_t>(ctx, total_rows) : 0;
                const uint64_t tail = show_tail ? std::min<uint64_t>(ctx, total_rows - head) : 0;
                const uint64_t omitted = total_rows - head - tail;
                if (head > 0) {
                    emit_rows(out, RowKind::Context, a.data(), seg.a_offset, seg.a_len, 0, head, bpr, width,
                              s, fill_width);
                }
                if (omitted > 0) {
                    const uint64_t resume = (head + omitted) * static_cast<uint64_t>(bpr);
                    push_header(fmt::format("@@ -{} +{} @@", hex_offset(seg.a_offset + resume, width),
                                            hex_offset(seg.b_offset + resume, width)));
                }
                if (tail > 0) {
                    emit_rows(out, RowKind::Context, a.data(), seg.a_offset, seg.a_len, head + omitted, tail,
                              bpr, width, s, fill_width);
                }
                break;
            }
        }
    }

    return out;
}
