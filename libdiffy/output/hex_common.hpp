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

inline char
ascii_char(uint8_t b) {
    return (b >= 0x20 && b <= 0x7e) ? static_cast<char>(b) : '.';
}

}  // namespace diffy
