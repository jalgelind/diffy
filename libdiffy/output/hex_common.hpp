#pragma once

#include <cstdint>
#include <string>

namespace diffy {

// Number of hex digits needed to print the largest offset in either file
// (minimum 4, so a short file still reads as an aligned column).
inline int
hex_offset_width(uint64_t max_offset) {
    int digits = 1;
    while (max_offset >= 16) {
        max_offset >>= 4;
        ++digits;
    }
    return digits < 4 ? 4 : digits;
}

namespace hex_detail {
// 256 two-char lowercase hex pairs packed into one string ("000102…ff"), built
// once. Formatting a byte is then a 2-char copy — no fmt machinery or heap
// allocation per byte, which dominates rendering on large diffs.
struct HexPairs {
    char v[512];
    HexPairs() {
        const char* d = "0123456789abcdef";
        for (int i = 0; i < 256; ++i) {
            v[2 * i] = d[(i >> 4) & 0xf];
            v[2 * i + 1] = d[i & 0xf];
        }
    }
};
inline const char*
pairs() {
    static const HexPairs t;
    return t.v;
}
}  // namespace hex_detail

// Append the two-hex-digit form of `b` to `out` (no allocation).
inline void
append_hex_byte(std::string& out, uint8_t b) {
    out.append(hex_detail::pairs() + 2 * b, 2);
}

// Append `offset` as lowercase hex, zero-padded to at least `width` digits
// (matches the previous fmt "{:0{}x}" — never truncates a larger value).
inline void
append_hex_offset(std::string& out, uint64_t offset, int width) {
    char buf[16];
    int n = 0;
    do {
        buf[n++] = hex_detail::pairs()[2 * (offset & 0xf) + 1];  // low nibble digit
        offset >>= 4;
    } while (offset != 0);
    for (int pad = n; pad < width; ++pad) {
        out.push_back('0');
    }
    for (int i = n - 1; i >= 0; --i) {
        out.push_back(buf[i]);
    }
}

inline std::string
hex_offset(uint64_t offset, int width) {
    std::string s;
    s.reserve(static_cast<size_t>(width) + 2);
    append_hex_offset(s, offset, width);
    return s;
}

// Two lowercase hex digits for one byte.
inline std::string
hex_byte(uint8_t b) {
    return std::string(hex_detail::pairs() + 2 * b, 2);
}

// One lowercase hex digit for the low nibble of `n` (for per-nibble highlighting).
inline char
hex_nibble(uint8_t n) {
    return "0123456789abcdef"[n & 0x0F];
}

inline char
ascii_char(uint8_t b) {
    return (b >= 0x20 && b <= 0x7e) ? static_cast<char>(b) : '.';
}

// How much of an Equal run to show as context around the surrounding changes.
// Shared by all three hex renderers so their trimming can't drift: `head`/`tail`
// rows of context are shown, `omitted` rows in between are collapsed to a
// "@@ … @@" marker. Row-based (bytes = rows * bytes_per_row) so unified and
// side-by-side agree. `is_first`/`is_last` suppress the head/tail at the file
// ends (nothing to give context to).
struct HexWindow {
    uint64_t head = 0;     // context rows at the start of the run
    uint64_t omitted = 0;  // rows collapsed to a marker
    uint64_t tail = 0;     // context rows at the end of the run
};

inline HexWindow
hex_equal_window(uint64_t len, int bytes_per_row, bool is_first, bool is_last, uint64_t ctx_rows) {
    const uint64_t bpr = bytes_per_row > 0 ? static_cast<uint64_t>(bytes_per_row) : 16;
    const uint64_t total = (len + bpr - 1) / bpr;
    const bool show_head = !is_first;
    const bool show_tail = !is_last;
    HexWindow w;
    if (!show_head && !show_tail) {
        return w;  // whole-file equal run: show nothing
    }
    if (show_head && show_tail && total <= 2 * ctx_rows) {
        w.head = total;  // small enough: show it all, no marker
        return w;
    }
    w.head = show_head ? (ctx_rows < total ? ctx_rows : total) : 0;
    const uint64_t rem = total - w.head;
    w.tail = show_tail ? (ctx_rows < rem ? ctx_rows : rem) : 0;
    w.omitted = total - w.head - w.tail;
    return w;
}

}  // namespace diffy
