#include "color.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace diffy;

// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797

const std::string kESC = "\033";

const std::array<std::tuple<const TermStyle::Attribute, const std::string, const std::string>, 8> kAttributes {{
    { TermStyle::Attribute::Bold,          "bold",          "1" },
    { TermStyle::Attribute::Dim,           "dim",           "2" },
    { TermStyle::Attribute::Italic,        "italic",        "3" },
    { TermStyle::Attribute::Underline,     "underline",     "4" },
    { TermStyle::Attribute::Blink,         "blink",         "5" },
    { TermStyle::Attribute::Inverse,       "inverse",       "7" },
    { TermStyle::Attribute::Hidden,        "hidden",        "8" },
    { TermStyle::Attribute::Strikethrough, "strikethrough", "9" }
}};

// Color identifiers for 4 bit terminals.
TermColor TermColor::kReset   = TermColor {TermColor::Kind::Reset, 0, 0, 0};
TermColor TermColor::kDefault = TermColor {TermColor::Kind::DefaultColor, 39, 49, 0};

TermColor TermColor::kBlack        = TermColor {30,  40};
TermColor TermColor::kRed          = TermColor {31,  41};
TermColor TermColor::kGreen        = TermColor {32,  42};
TermColor TermColor::kYellow       = TermColor {33,  43};
TermColor TermColor::kBlue         = TermColor {34,  44};
TermColor TermColor::kMagenta      = TermColor {35,  45};
TermColor TermColor::kCyan         = TermColor {36,  46};
TermColor TermColor::kLightGray    = TermColor {37,  47};
TermColor TermColor::kDarkGray     = TermColor {90, 100};
TermColor TermColor::kLightRed     = TermColor {91, 101};
TermColor TermColor::kLightGreen   = TermColor {92, 102};
TermColor TermColor::kLightYellow  = TermColor {93, 103};
TermColor TermColor::kLightBlue    = TermColor {94, 104};
TermColor TermColor::kLightMagenta = TermColor {95, 105};
TermColor TermColor::kLightCyan    = TermColor {96, 106};
TermColor TermColor::kWhite        = TermColor {97, 107};

// Color identifiers for true color terminals.
TermColor TermColor::kBlack24        = TermColor {  0  , 0,   0};
TermColor TermColor::kRed24          = TermColor {205,  49,  49};
TermColor TermColor::kGreen24        = TermColor { 13, 188, 121};
TermColor TermColor::kYellow24       = TermColor {229, 229,  16};
TermColor TermColor::kBlue24         = TermColor { 36, 114, 200};
TermColor TermColor::kMagenta24      = TermColor {188,  63, 188};
TermColor TermColor::kCyan24         = TermColor {  17,168, 205};
TermColor TermColor::kLightGray24    = TermColor {229, 229, 229};
TermColor TermColor::kDarkGray24     = TermColor {102, 102, 102};
TermColor TermColor::kLightRed24     = TermColor {241,  76,  76};
TermColor TermColor::kLightGreen24   = TermColor { 35, 209, 139};
TermColor TermColor::kLightYellow24  = TermColor {245, 245,  67};
TermColor TermColor::kLightBlue24    = TermColor { 59, 142, 234};
TermColor TermColor::kLightMagenta24 = TermColor {214, 112, 214};
TermColor TermColor::kLightCyan24    = TermColor { 41, 184, 219};
TermColor TermColor::kWhite24        = TermColor {229, 229, 229};
// clang-format on

// clang-format off
const std::unordered_map<
        std::string,
        diffy::TermColor> k16Colors = {            
        { "reset",         TermColor::kReset },
        { "default",       TermColor::kDefault },
        { "black",         TermColor::kBlack },
        { "red",           TermColor::kRed },
        { "green",         TermColor::kGreen },
        { "yellow",        TermColor::kYellow },
        { "blue",          TermColor::kBlue },
        { "magenta",       TermColor::kMagenta },
        { "cyan",          TermColor::kCyan },
        { "light_gray",    TermColor::kLightGray },
        { "dark_gray",     TermColor::kDarkGray },
        { "light_red",     TermColor::kLightRed },
        { "light_green",   TermColor::kLightGreen },
        { "light_yellow",  TermColor::kLightYellow },
        { "light_blue",    TermColor::kLightBlue },
        { "light_magenta", TermColor::kLightMagenta },
        { "light_cyan",    TermColor::kLightCyan },
        { "white",         TermColor::kWhite }
};

std::string
diffy::repr(const TermColor& color) {
    std::string ks[] = { "4", "T", "D", "R" };
    std::string k = ks[(int) color.kind];
    return fmt::format("{}:({},{},{})", k, color.r, color.g, color.b);
}

static
std::tuple<TermColor, TermColor>
get_term_color_palette(std::string fg, std::string bg) {
    TermColor fg_color = TermColor::kDefault;
    TermColor bg_color = TermColor::kDefault;

    if (!fg.empty() && k16Colors.find(fg) != k16Colors.end()) {
        fg_color = k16Colors.at(fg);
    } else {
        // TODO: Error; no such color found
    }

    if (!bg.empty() && k16Colors.find(bg) != k16Colors.end()) {
        bg_color = k16Colors.at(bg);
    } else {
        // TODO: Error; no such color found
    }

    return std::tuple(fg_color, bg_color);
}

static
std::string
get_term_color_name(const TermColor& expected) {
    for (const auto &[name, color] : k16Colors) {
        if (color == expected) {
            return name;
        }
    }
    return "default";
}

static
TermStyle::Attribute
color_encode_attributes(const std::vector<std::string>& attributes) {
    uint16_t result = 0;
    for (const auto& [flag, name, _id] : kAttributes) {
        if (std::find(attributes.begin(), attributes.end(), name) != attributes.end()) {
            result |= (uint16_t) flag;
        }
    }
    return (TermStyle::Attribute) result;
}

static
std::vector<std::string>
color_decode_attributes(TermStyle::Attribute attr) {
    std::vector<std::string> result;
    for (const auto& [flag, name, _id] : kAttributes) {
        if ((uint16_t) flag & (uint16_t) attr) {
            result.push_back(name);
        }
    }
    return result;
}

TermStyle TermStyle::from_value(Value::Table table) {
    TermStyle c;

    // Translate color object to ansi code
    auto fg = table["fg"].as_string();
    auto bg = table["bg"].as_string();
    auto attr_value_array = table["attr"];

    std::vector<std::string> attributes;
    for (auto& attr_node : attr_value_array.as_array()) {
        attributes.push_back(attr_node.as_string());
    }
    TermStyle::Attribute attr = color_encode_attributes(attributes);
    
    if (fg.empty() && bg.empty())
    {
        return c;
    }

    // FIXME: This is where we force 16 palette colors.
    auto [fg_color, bg_color] = get_term_color_palette(fg, bg);

    c.fg = fg_color;
    c.bg = bg_color;
    c.attr = (TermStyle::Attribute) attr;

    return c;
}

std::string TermStyle::to_ansi() {
    std::string result;

    std::string kESC1 = "\\";

    switch (fg.kind) {
        // 24 bit
        // ESC[38;2;{r};{g};{b}m	Set foreground color as RGB.
        // ESC[48;2;{r};{g};{b}m	Set background color as RGB.
        case TermColor::Kind::Color24bit: {
            result += kESC + fmt::format("[38;2;{};{};{}m", fg.r, fg.g, fg.b);
            result += kESC + fmt::format("[48;2;{};{};{}m", bg.r, bg.g, bg.b);
        } break;

        // 256 color palette (unsupported)
        // ESC[38;5;{ID}m	Set foreground color.
        // ESC[48;5;{ID}m	Set background color.

        // 16 color palette ('4 bit')
        // ESC=\033
        // ESC[{ID};{ID}m  Set foreground and background color
        case TermColor::Kind::DefaultColor:
        case TermColor::Kind::Reset:
        case TermColor::Kind::Color4bit: {
            std::vector<std::string> codes;
                codes.push_back(fmt::format("{}", fg.r));
                if (bg.g != 0) {
                    // TODO: Only set this when using a background?
                    codes.push_back(fmt::format("{}", fg.g));
                }

                for (const auto& [attr_flag, attr_name, attr_code] : kAttributes) {
                    if ((uint16_t) attr & (uint16_t) attr_flag) {
                        codes.push_back(attr_code);
                    }
                }

                result += kESC + "[";
                for (auto code : codes) {
                    result += ";";
                    result += code;
                }
                result += "m";
        } break;
        default: assert(false && "missing case");
    };

    return result;
}

Value
TermStyle::to_value() {
    Value::Array attr_arr;
    for (const auto& attr : color_decode_attributes(attr)) {
        attr_arr.push_back({attr});
    }

    Value v;
    v["fg"] = { get_term_color_name(fg) };
    v["bg"] = { get_term_color_name(bg) };
    v["attr"] = { attr_arr };
    return v;
}

void
diffy::dump_colors() {
    fmt::print("Available values (16pal)\n");
    for (const auto& [k, v] : k16Colors) {
        auto s = TermStyle { v, TermColor::kReset };
        fmt::print("{}{}, ", s.to_ansi(), k);
    }
    fmt::print("\n");
}
