#include "color.hpp"

#include <fmt/format.h>

#include <array>
#include <tuple>
#include <unordered_map>
#include <vector>

// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797

const std::string kESC = "\033";

// clang-format off
const std::unordered_map<std::string, std::tuple<std::string, std::string>> k16Colors = {
        { "reset",         {  "0",  "0" }},
        { "black",         { "30", "40" }},
        { "red",           { "31", "41" }},
        { "green",         { "32", "42" }},
        { "yellow",        { "33", "43" }},
        { "blue",          { "34", "44" }},
        { "magenta",       { "35", "45" }},
        { "cyan",          { "36", "46" }},
        { "light_gray",    { "37", "47" }},
        { "default",       { "39", "49" }},
        { "dark_gray",     { "90", "100" }},
        { "light_red",     { "91", "101" }},
        { "light_green",   { "92", "102" }},
        { "light_yellow",  { "93", "103" }},
        { "light_blue",    { "94", "104" }},
        { "light_magenta", { "95", "104" }},
        { "light_cyan",    { "96", "106" }},
        { "white",         { "97", "107" }}
};

const std::array<std::tuple<const int32_t, const std::string, const std::string>, 8> kAttributes{
    {   {diffy::kStyleFlag_BOLD,          "bold",          "1"},
        {diffy::kStyleFlag_DIM,           "dim",           "2"},
        {diffy::kStyleFlag_ITALIC,        "italic",        "3"},
        {diffy::kStyleFlag_UNDERLINE,     "underline",     "4"},
        {diffy::kStyleFlag_BLINK,         "blink",         "5"},
        {diffy::kStyleFlag_INVERSE,       "inverse",       "7"},
        {diffy::kStyleFlag_HIDDEN,        "hidden",        "8"},
        {diffy::kStyleFlag_STRIKETHROUGH, "strikethrough", "9"}}
};
// clang-format on

uint32_t
diffy::color_lookup_attributes(const std::vector<std::string>& attributes) {
    uint32_t result = 0;
    for (const auto& [flag, name, _id] : kAttributes) {
        if (std::find(attributes.begin(), attributes.end(), name) != attributes.end()) {
            result |= flag;
        }
    }
    return result;
}

bool
diffy::translate_color_name_to_16pal_escape_sequence(const std::string& fg_color,
                                                     const std::string& bg_color,
                                                     uint32_t flags,
                                                     std::string& result) {
    if (fg_color.empty() && bg_color.empty())
        return false;

    std::string fg_id;
    std::string bg_id;

    if (!fg_color.empty() && k16Colors.find(fg_color) != k16Colors.end()) {
        fg_id = std::get<0>(k16Colors.at(fg_color));
    }

    if (!bg_color.empty() && k16Colors.find(bg_color) != k16Colors.end()) {
        bg_id = std::get<1>(k16Colors.at(bg_color));
    }

    if (fg_id.empty() && bg_id.empty()) {
        return false;
    }

    result += kESC + "[";

    std::vector<std::string> codes{};

    if (!fg_id.empty()) {
        codes.push_back(fg_id);
    }

    if (!bg_id.empty()) {
        codes.push_back(bg_id);
    }

    for (const auto& [attr_flag, attr_name, attr_code] : kAttributes) {
        if (flags & attr_flag) {
            codes.push_back(attr_code);
        }
    }

    for (auto code : codes) {
        result += ";";
        result += code;
    }

    result += "m";

    return true;
}

void
diffy::dump_colors() {
    fmt::print("Available values (16pal)\n");
    for (const auto& [k, _v] : k16Colors) {
        fmt::print("- {}\n", k);
    }
}

/*

>>> _fg_colors
{'default': 39, 'black': 30, 'red': 31, 'green': 32, 'yellow': 33, 'blue': 34, 'magenta': 35, 'cyan': 36,
'light_gray': 37, 'dark_gray': 90, 'light_red': 91, 'light_green': 92, 'light_yellow': 93, 'light_blue': 94,
'light_magenta': 95, 'light_cyan': 96, 'white': 97}
>>> _bg_colors
{'default': 49, 'black': 40, 'red': 41, 'green': 42, 'yellow': 43, 'blue': 44, 'magenta': 45, 'cyan': 46,
'light_gray': 47, 'dark_gray': 100, 'light_red': 101, 'light_green': 102, 'light_yellow': 103, 'light_blue':
104, 'light_magenta': 105, 'light_cyan': 106, 'white': 107}

// name            fg    bg

*/

// 16
// ESC=\033
// ESC[1;{ID};{ID}m  Set foreground and background color

// 256
// ESC[38;5;{ID}m	Set foreground color.
// ESC[48;5;{ID}m	Set background color.

// truecolor
// ESC[38;2;{r};{g};{b}m	Set foreground color as RGB.
// ESC[48;2;{r};{g};{b}m	Set background color as RGB.