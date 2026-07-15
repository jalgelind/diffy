#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// Largest bytes-per-row whose full unified row fits `width_cols` columns, so the
// ASCII column is never pushed off-screen. A row is prefix(1) + offset(off_w) +
// "  "(2) + 3*bpr hex + "|" + bpr ascii + "|" = (off_w + 5) + 4*bpr, and
// `width_cols` is the per-pane budget (a side-by-side view gets two of these).
// Prefer a multiple of 8; fall back to whatever fits on a very narrow pane.
// `width_cols <= 0` means the width is unknown (no tty / not laid out yet), so
// return a sane default of 16.
inline int
hex_fit_bytes_per_row(int width_cols, int off_w) {
    if (width_cols <= 0) {
        return 16;  // width unknown (no tty / not laid out yet): sane default
    }
    const long fit = (static_cast<long>(width_cols) - (off_w + 5)) / 4;
    if (fit >= 8) {
        return static_cast<int>((fit / 8) * 8);
    }
    return fit >= 1 ? static_cast<int>(fit) : 1;
}

// Thresholds for collapsing a hopeless hex diff to a one-line "binary files
// differ" summary instead of emitting hundreds of thousands of useless rows.
// Shared by the CLI and GUI so their cutoffs can't drift.
inline constexpr uint64_t kHexCoarseSummariseCap = 512ull * 1024;      // coarse + this big
inline constexpr uint64_t kHexHardSummariseCap = 8ull * 1024 * 1024;   // huge regardless

// True when a hex diff of `change_bytes` changed bytes should be summarised
// rather than rendered row-by-row: either the alignment was `truncated` (a
// region exceeded the byte-refine budget) and the change exceeds the coarse cap,
// or the change is huge regardless of truncation.
inline bool
hex_should_summarise(uint64_t change_bytes, bool truncated) {
    return (truncated && change_bytes > kHexCoarseSummariseCap) ||
           change_bytes > kHexHardSummariseCap;
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

// One emitted hex row: `count` bytes starting at absolute file offset `offset`.
struct HexRow {
    uint64_t offset;
    uint64_t count;
};

// Split a run of `len` bytes starting at file offset `base_offset` into display
// rows aligned to the bytes-per-row grid, like xxd: the first row runs only up to
// the next multiple of `bytes_per_row`, and every row after it starts on a grid
// boundary. Without this a run whose start isn't a multiple of bpr (any run after
// a length change) carries that phase forever, so the offset column jumps to
// ...0c, ...1c, ... instead of the clean ...10, ...20 xxd shows. The one short
// catch-up row is the price of realigning; equal regions on either side of a
// change then line up column-for-column.
inline std::vector<HexRow>
hex_grid_rows(uint64_t base_offset, uint64_t len, int bytes_per_row) {
    const uint64_t bpr = bytes_per_row > 0 ? static_cast<uint64_t>(bytes_per_row) : 16;
    std::vector<HexRow> rows;
    uint64_t off = base_offset;
    uint64_t remaining = len;
    while (remaining > 0) {
        const uint64_t to_boundary = bpr - (off % bpr);  // room left in this grid row
        const uint64_t n = to_boundary < remaining ? to_boundary : remaining;
        rows.push_back({off, n});
        off += n;
        remaining -= n;
    }
    return rows;
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
hex_equal_window(uint64_t total, bool is_first, bool is_last, uint64_t ctx_rows) {
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
