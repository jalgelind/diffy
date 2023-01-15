#pragma once

#include "config_parser/config_parser.hpp"

#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

namespace diffy {

struct TermColor {
    enum class Kind : uint8_t {
        Color4bit = 0,
        Color24bit,
        DefaultColor,
        Reset
    };

    Kind kind;

    uint8_t r;
    uint8_t g;
    uint8_t b;

    TermColor() {
        *this = TermColor::kDefault;
    }

    // Custom ones    
    TermColor(Kind kind, uint8_t r, uint8_t g, uint8_t b)
        : kind(kind)
        , r(r)
        , g(g)
        , b(b) {}

    bool operator == (const TermColor& other) const {
        return other.kind == kind && other.r == r && other.g == g && other.b == b;
    }

    // Parse color from configuration table value
    static std::optional<TermColor>
    from_value(Value value);

    // 
    static TermColor kReset;
    static TermColor kDefault;

    // Colors (standard 4 bit palette)
    static TermColor kBlack;
    static TermColor kRed;
    static TermColor kGreen;
    static TermColor kYellow;
    static TermColor kBlue;
    static TermColor kMagenta;
    static TermColor kCyan;
    static TermColor kLightGray;
    static TermColor kDarkGray;
    static TermColor kLightRed;
    static TermColor kLightGreen;
    static TermColor kLightYellow;
    static TermColor kLightBlue;
    static TermColor kLightMagenta;
    static TermColor kLightCyan;
    static TermColor kWhite;
};

std::string
repr(const TermColor& color);

struct TermStyle {

    enum class Attribute : uint16_t {
        None          = 0,
        Bold          = 1 << 0,
        Dim           = 1 << 1,
        Italic        = 1 << 2,
        Underline     = 1 << 4,
        Blink         = 1 << 5,
        Inverse       = 1 << 6,
        Hidden        = 1 << 7,
        Strikethrough = 1 << 8,
    };

    TermColor fg;
    TermColor bg;
    Attribute attr;

    TermStyle()
    : TermStyle(TermColor::kDefault, TermColor::kDefault) {}

    // Colors and with attributes
    explicit TermStyle(TermColor fg, TermColor bg, Attribute attr)
        : fg(fg)
        , bg(bg)
        , attr(attr) {}

    // Fore- and background color
    explicit TermStyle(TermColor fg, TermColor bg)
        : TermStyle(fg, bg, Attribute::None) {}

    // Convert color to ansi escape sequence
    std::string
    to_ansi();

    // Parse style from configuration table value
    static std::optional<TermStyle>
    from_value(Value::Table table);

    Value
    to_value();
};

void
dump_colors();

}  // namespace diffy
