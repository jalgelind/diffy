#pragma once

#include "config/config.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

// TODO: rename to Style; or AnsiStyle or something...
struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t attr;

    // Configuration style
    static Color from_value(Value::Table table) {
        Color c;

        // Translate color object to ansi code
        auto fg = table["fg"].as_string();
        auto bg = table["bg"].as_string();
        auto attr = table["attr"];

        std::vector<std::string> attributes;
        for (auto& attr_node : attr.as_array()) {
            attributes.push_back(attr_node.as_string());
        }
        //diffy::translate_color_name_to_16pal_escape_sequence(
        //    fg, bg, diffy::color_lookup_attributes(attributes), parsed_color);

        return c;
    }

    const static Color kReset;
    const static Color kBlack;
    const static Color kRed;
    const static Color kGreen;
    const static Color kYellow;
    const static Color kBlue;
    const static Color kMagenta;
    const static Color kCyan;
    const static Color kLightGray;
    const static Color kDefault;
    const static Color kDarkGray;
    const static Color kLightRed;
    const static Color kLightGreen;
    const static Color kLightYellow;
    const static Color kLightBlue;
    const static Color kLightMagenta;
    const static Color kLightCyan;
    const static Color kWhite;

};

const uint32_t kStyleFlag_BOLD = 1 << 1;
const uint32_t kStyleFlag_DIM = 1 << 2;
const uint32_t kStyleFlag_ITALIC = 1 << 3;
const uint32_t kStyleFlag_UNDERLINE = 1 << 4;
const uint32_t kStyleFlag_BLINK = 1 << 5;  // really?
const uint32_t kStyleFlag_INVERSE = 1 << 7;
const uint32_t kStyleFlag_HIDDEN = 1 << 8;
const uint32_t kStyleFlag_STRIKETHROUGH = 1 << 9;

uint32_t
color_lookup_attributes(const std::vector<std::string>& attributes);

// 16 color palette
bool
translate_color_name_to_16pal_escape_sequence(const std::string& fg_color,
                                              const std::string& bg_color,
                                              uint32_t flags,
                                              std::string& result);

#if 0
// 256 color palette
bool
translate_color_name_to_256pal_escape_sequence(const std::string& fg_color,
                                               const std::string& bg_color,
                                               std::string& result);
// truetype color palette
// Only accepts a 24 bit hex value, #rrggbb
bool
translate_color_name_to_tt_escape_sequence(const std::string& fg_color,
                                           const std::string& bg_color,
                                           std::string& result);
#endif

void
dump_colors();

}  // namespace diffy
