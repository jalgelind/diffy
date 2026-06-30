#include "text_layout.hpp"

namespace diffy::gui {

namespace {

// Bytes in the UTF-8 code point starting at s[i] (lead byte + continuations).
size_t
cp_len(const std::string& s, size_t i) {
    size_t n = 1;
    while (i + n < s.size() && (static_cast<unsigned char>(s[i + n]) & 0xc0) == 0x80) {
        ++n;
    }
    return n;
}

// One display column: its bytes, resolved style, and whether it's a break space.
struct Cell {
    std::string bytes;
    uint32_t argb;
    bool bold;
    bool space;
};

// Coalesce consecutive same-style cells into runs.
std::vector<DisplayRun>
emit_line(const std::vector<Cell>& cells) {
    std::vector<DisplayRun> line;
    for (const auto& c : cells) {
        if (!line.empty() && line.back().argb == c.argb && line.back().bold == c.bold) {
            line.back().text += c.bytes;
        } else {
            line.push_back(DisplayRun{c.bytes, c.argb, c.bold});
        }
    }
    return line;
}

}  // namespace

std::vector<std::vector<DisplayRun>>
wrap_display_runs(const std::vector<DisplayRun>& runs, int wrap_cols) {
    if (wrap_cols < 1) {
        std::vector<DisplayRun> single;
        for (const auto& r : runs) {
            if (!r.text.empty()) {
                single.push_back(r);
            }
        }
        return {std::move(single)};
    }

    // Flatten to display columns (code points).
    std::vector<Cell> cells;
    for (const auto& r : runs) {
        for (size_t i = 0; i < r.text.size();) {
            const size_t n = cp_len(r.text, i);
            std::string b = r.text.substr(i, n);
            const bool sp = (n == 1 && b[0] == ' ');
            cells.push_back(Cell{std::move(b), r.argb, r.bold, sp});
            i += n;
        }
    }

    std::vector<std::vector<DisplayRun>> lines;
    std::vector<Cell> cur;
    int last_space = -1;  // index in `cur` of the last space (soft-break point)

    auto reset_last_space = [&]() {
        last_space = -1;
        for (int j = 0; j < static_cast<int>(cur.size()); ++j) {
            if (cur[j].space) {
                last_space = j;
            }
        }
    };

    for (const auto& cell : cells) {
        cur.push_back(cell);
        if (cell.space) {
            last_space = static_cast<int>(cur.size()) - 1;
        }
        if (static_cast<int>(cur.size()) <= wrap_cols) {
            continue;
        }
        // Overflowed by one column; break.
        if (last_space >= 0 && last_space + 1 < static_cast<int>(cur.size())) {
            // Soft break: emit up to the space (dropped), carry the rest.
            std::vector<Cell> head(cur.begin(), cur.begin() + last_space);
            std::vector<Cell> tail(cur.begin() + last_space + 1, cur.end());
            lines.push_back(emit_line(head));
            cur = std::move(tail);
        } else if (cur.back().space) {
            // The overflowing column is itself a space: drop it, keep the line.
            cur.pop_back();
            lines.push_back(emit_line(cur));
            cur.clear();
        } else {
            // Hard break: emit the full-width head, carry the overflow column.
            Cell carry = cur.back();
            cur.pop_back();
            lines.push_back(emit_line(cur));
            cur.clear();
            cur.push_back(std::move(carry));
        }
        reset_last_space();
    }
    lines.push_back(emit_line(cur));
    return lines;
}

std::string
expand_for_display(const std::string& in, int tab_width, int& col) {
    std::string out;
    out.reserve(in.size());
    if (tab_width < 1) {
        tab_width = 1;
    }
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '\t') {
            const int n = tab_width - (col % tab_width);
            out.append(static_cast<size_t>(n), ' ');
            col += n;
            ++i;
        } else if (c < 0x20 || c == 0x7f) {
            out.push_back(' ');
            ++col;
            ++i;
        } else {
            out.push_back(static_cast<char>(c));
            ++col;
            ++i;
            // Keep UTF-8 continuation bytes with the lead byte; they don't add a
            // display column.
            while (i < in.size() && (static_cast<unsigned char>(in[i]) & 0xc0) == 0x80) {
                out.push_back(in[i]);
                ++i;
            }
        }
    }
    return out;
}

}  // namespace diffy::gui
