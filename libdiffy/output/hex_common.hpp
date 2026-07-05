#pragma once

#include <cstdint>
#include <string>

#include <fmt/format.h>

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

inline std::string
hex_offset(uint64_t offset, int width) {
    return fmt::format("{:0{}x}", offset, width);
}

// Two lowercase hex digits for one byte.
inline std::string
hex_byte(uint8_t b) {
    return fmt::format("{:02x}", b);
}

inline char
ascii_char(uint8_t b) {
    return (b >= 0x20 && b <= 0x7e) ? static_cast<char>(b) : '.';
}

}  // namespace diffy
