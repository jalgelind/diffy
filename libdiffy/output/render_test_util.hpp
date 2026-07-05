#pragma once

// Shared test-side helpers for column-view rendering tests: ANSI stripping, an
// SGR-background oracle, and inverted-theme factories. Header-only; every
// function is `inline` so multiple test translation units can include it.
// TEST-ONLY — not compiled into the production binary.

#include "config/config.hpp"
#include "output/column_view.hpp"
#include "util/color.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace diffy::test {

// Strip CSI escape sequences (ESC [ ... <final>), leaving the visible text — the
// width a terminal would render.
inline std::string
strip_ansi(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            std::size_t j = i + 2;
            while (j < s.size() && !(s[j] >= '@' && s[j] <= '~'))
                j++;
            i = (j < s.size()) ? j + 1 : j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// Parse a styled row into (background-id, visible-codepoint-count) runs, tracking
// the active SGR background exactly as a terminal would. Background id: "" for
// the terminal default, "8:<n>" 256-color, "24:<r>,<g>,<b>" truecolor, "4:<n>"
// 16-color. A run is recorded only when visible characters are emitted, so a
// trailing reset adds no run. This is the oracle the inverted-theme assertions
// check against.
inline std::vector<std::pair<std::string, int>>
bg_runs(const std::string& row) {
    std::vector<std::pair<std::string, int>> runs;
    std::string bg;  // active background ("" == terminal default)

    auto emit = [&](int n) {
        if (n <= 0)
            return;
        if (!runs.empty() && runs.back().first == bg)
            runs.back().second += n;
        else
            runs.push_back({bg, n});
    };

    for (std::size_t i = 0; i < row.size();) {
        if (row[i] == '\033' && i + 1 < row.size() && row[i + 1] == '[') {
            std::size_t j = i + 2;
            while (j < row.size() && !(row[j] >= '@' && row[j] <= '~'))
                j++;
            char final_byte = (j < row.size()) ? row[j] : '\0';
            std::string params = row.substr(i + 2, j - (i + 2));
            i = (j < row.size()) ? j + 1 : j;
            if (final_byte != 'm')
                continue;  // not an SGR sequence

            std::vector<int> codes;
            std::string cur;
            for (char c : params) {
                if (c == ';') {
                    codes.push_back(cur.empty() ? 0 : std::stoi(cur));
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            codes.push_back(cur.empty() ? 0 : std::stoi(cur));
            if (params.empty())
                codes = {0};

            for (std::size_t k = 0; k < codes.size(); k++) {
                int c = codes[k];
                if (c == 0 || c == 49) {
                    bg = "";  // reset-all / default-bg
                } else if ((c >= 40 && c <= 47) || (c >= 100 && c <= 107)) {
                    bg = "4:" + std::to_string(c);
                } else if (c == 48 && k + 2 < codes.size() && codes[k + 1] == 5) {
                    bg = "8:" + std::to_string(codes[k + 2]);
                    k += 2;
                } else if (c == 48 && k + 4 < codes.size() && codes[k + 1] == 2) {
                    bg = "24:" + std::to_string(codes[k + 2]) + "," + std::to_string(codes[k + 3]) + "," +
                         std::to_string(codes[k + 4]);
                    k += 4;
                } else if (c == 38 && k + 1 < codes.size() && codes[k + 1] == 5) {
                    k += 2;  // foreground 256-color: skip its params, bg unchanged
                } else if (c == 38 && k + 1 < codes.size() && codes[k + 1] == 2) {
                    k += 4;  // foreground truecolor: skip its params, bg unchanged
                }
                // NOTE: the `inverse` attribute (SGR 7) is intentionally ignored — these
                // tests express fills with explicit bg codes, not by swapping fg/bg. A
                // theme that filled via inverse would need this oracle extended.
            }
        } else {
            unsigned char ch = (unsigned char) row[i];
            if ((ch & 0xC0) != 0x80)  // count UTF-8 lead bytes only (one per codepoint)
                emit(1);
            i++;
        }
    }
    return runs;
}

inline std::set<std::string>
bg_set(const std::string& row) {
    std::set<std::string> s;
    for (const auto& [bg, len] : bg_runs(row))
        if (len > 0)
            s.insert(bg);
    return s;
}

// True if any visible character is emitted on the terminal-default background —
// a hole in an inverted theme (padding spaces included, where the TH-1 bug hid).
inline bool
has_default_bg(const std::string& row) {
    for (const auto& [bg, len] : bg_runs(row))
        if (bg.empty() && len > 0)
            return true;
    return false;
}

inline TermColor
pal(int idx) {
    return TermColor(TermColor::Kind::Color8bit, (uint8_t) idx, 0, 0);
}

inline TermColor
rgb(int r, int g, int b) {
    return TermColor(TermColor::Kind::Color24bit, (uint8_t) r, (uint8_t) g, (uint8_t) b);
}

inline std::string
ansi(TermColor fg, TermColor bg, TermStyle::Attribute attr = TermStyle::Attribute::None) {
    TermStyle s(fg, bg, attr);
    return s.to_ansi();
}

// A fully inverted theme using 256-color (48;5;n) backgrounds — a distinct,
// identifiable id per cell key. Token styles set only a foreground (no bg) so a
// highlighted token keeps its line's background, proving the "layer, don't
// replace" behavior. The global `background` knob is left unset on purpose.
inline ColumnViewState
inverted_theme() {
    ColumnViewState c;
    auto& s = c.style;
    s.header = ansi(TermColor::kWhite, pal(237), TermStyle::Attribute::Underline);
    s.delete_line = ansi(TermColor::kWhite, pal(52));
    s.delete_token = ansi(pal(88), TermColor::kNone, TermStyle::Attribute::Bold);
    s.delete_line_number = ansi(TermColor::kWhite, pal(53));
    s.insert_line = ansi(TermColor::kWhite, pal(22));
    s.insert_token = ansi(pal(28), TermColor::kNone, TermStyle::Attribute::Bold);
    s.insert_line_number = ansi(TermColor::kWhite, pal(23));
    s.common_line = ansi(TermColor::kWhite, pal(236));
    s.common_line_number = ansi(TermColor::kWhite, pal(235));
    s.frame = ansi(TermColor::kWhite, pal(240));
    s.empty_cell = ansi(TermColor::kWhite, pal(234));
    return c;
}

// Same shape, but truecolor (48;2;r;g;b) backgrounds — the format that hex theme
// colors (e.g. extras/theme_inverted.conf's '#1c1c1c') compile to in config.cc.
// Exercises the 48;2 composition path the palette theme above never touches.
inline ColumnViewState
inverted_theme_truecolor() {
    ColumnViewState c;
    auto& s = c.style;
    s.header = ansi(TermColor::kWhite, rgb(38, 79, 120), TermStyle::Attribute::Underline);
    s.delete_line = ansi(TermColor::kWhite, rgb(59, 18, 18));
    s.delete_token = ansi(rgb(255, 128, 128), TermColor::kNone, TermStyle::Attribute::Bold);
    s.delete_line_number = ansi(rgb(255, 128, 128), rgb(59, 18, 18));
    s.insert_line = ansi(TermColor::kWhite, rgb(15, 45, 15));
    s.insert_token = ansi(rgb(128, 255, 128), TermColor::kNone, TermStyle::Attribute::Bold);
    s.insert_line_number = ansi(rgb(128, 255, 128), rgb(15, 45, 15));
    s.common_line = ansi(TermColor::kWhite, rgb(28, 28, 28));
    s.common_line_number = ansi(TermColor::kWhite, rgb(35, 35, 35));
    s.frame = ansi(TermColor::kWhite, rgb(40, 40, 40));
    s.empty_cell = ansi(TermColor::kWhite, rgb(28, 28, 28));
    return c;
}

}  // namespace diffy::test
