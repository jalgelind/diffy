#include "diff_bridge.hpp"

#include "text_layout.hpp"

#include "config/config.hpp"
#include "highlight/highlight_palette.hpp"
#include "util/color.hpp"

#include <algorithm>
#include <string>

namespace diffy::gui {

namespace {

slint::SharedString
shared(const std::string& s) {
    return slint::SharedString(s.c_str());
}

slint::Color
rgb(uint8_t r, uint8_t g, uint8_t b) {
    return slint::Color::from_argb_uint8(255, r, g, b);
}

slint::Color
transparent() {
    return slint::Color::from_argb_uint8(0, 0, 0, 0);
}

// One ANSI-16 SGR code (30-37/90-97 fg or 40-47/100-107 bg) -> an xterm RGB.
slint::Color
ansi16(int code) {
    static const uint8_t base[8][3] = {{0, 0, 0},     {205, 49, 49},  {13, 188, 121}, {229, 229, 16},
                                       {36, 114, 200}, {188, 63, 188}, {17, 168, 205}, {229, 229, 229}};
    static const uint8_t bright[8][3] = {{102, 102, 102}, {241, 76, 76},   {35, 209, 139},
                                         {245, 245, 67},  {59, 142, 234},  {214, 112, 214},
                                         {41, 184, 219},  {255, 255, 255}};
    bool br = (code >= 90 && code <= 97) || (code >= 100 && code <= 107);
    int idx = code % 10;
    if (idx > 7) {
        idx = 7;
    }
    const uint8_t* t = br ? bright[idx] : base[idx];
    return rgb(t[0], t[1], t[2]);
}

// xterm 256-palette index -> RGB.
slint::Color
xterm256(int i) {
    if (i < 8) return ansi16(30 + i);
    if (i < 16) return ansi16(90 + (i - 8));
    if (i >= 232) {
        uint8_t v = static_cast<uint8_t>(8 + (i - 232) * 10);
        return rgb(v, v, v);
    }
    i -= 16;
    auto conv = [](int x) -> uint8_t { return x == 0 ? 0 : static_cast<uint8_t>(55 + x * 40); };
    return rgb(conv(i / 36), conv((i % 36) / 6), conv(i % 6));
}

// A theme TermColor -> Slint colour. 24-bit carries real RGB; 4-/8-bit carry SGR
// codes / palette indices in r; the terminal default falls back to `fallback`.
slint::Color
to_color(const diffy::TermColor& c, slint::Color fallback) {
    using K = diffy::TermColor::Kind;
    switch (c.kind) {
        case K::Color24bit:
            return rgb(c.r, c.g, c.b);
        case K::Color8bit:
            return xterm256(c.r);
        case K::Color4bit:
            return ansi16(c.r);  // r holds the SGR code; its colour family is what we want
        default:
            return fallback;  // DefaultColor / Reset / Ignore
    }
}

bool
is_bold(diffy::SpanStyle style) {
    return style == diffy::SpanStyle::DeleteToken || style == diffy::SpanStyle::InsertToken;
}

slint::Color
cell_bg(const GuiTheme& t, const diffy::DiffCell& cell) {
    if (!cell.present) {
        return transparent();
    }
    switch (cell.type) {
        case diffy::EditType::Delete:
            return t.delete_bg;
        case diffy::EditType::Insert:
            return t.insert_bg;
        default:
            return transparent();  // common line -> window background shows through
    }
}

// Foreground for a span: changed tokens keep the theme's add/remove accent (and
// bold), syntax-highlighted text uses the tree-sitter palette, everything else
// is the theme's default text colour.
slint::Color
span_fg(const GuiTheme& t, const diffy::StyledSpan& s) {
    if (s.style == diffy::SpanStyle::DeleteToken) {
        return t.delete_token;
    }
    if (s.style == diffy::SpanStyle::InsertToken) {
        return t.insert_token;
    }
    if (s.syntax != diffy::HighlightGroup::None) {
        const diffy::HlRgb c = diffy::syntax_color(s.syntax, !t.dark);
        return rgb(c.r, c.g, c.b);
    }
    return t.fg;
}

uint32_t
pack(slint::Color c) {
    return (static_cast<uint32_t>(c.alpha()) << 24) | (static_cast<uint32_t>(c.red()) << 16) |
           (static_cast<uint32_t>(c.green()) << 8) | static_cast<uint32_t>(c.blue());
}

slint::Color
unpack(uint32_t a) {
    return slint::Color::from_argb_uint8((a >> 24) & 0xff, (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff);
}

// Build a cell's visual lines: expand tabs, resolve each span's colour, and (when
// wrapping) split into sub-lines at `wrap_cols` columns while keeping colours.
// `out_width` reports the unwrapped display width (for the horizontal extent).
std::shared_ptr<slint::VectorModel<DiffLine>>
make_lines(const GuiTheme& t, const diffy::DiffCell& cell, int tab_width, bool wrap, int wrap_cols,
           int& out_width) {
    std::vector<DisplayRun> runs;
    int col = 0;
    for (const auto& s : cell.spans) {
        std::string piece = expand_for_display(s.text, tab_width, col);
        if (piece.empty()) {
            continue;
        }
        runs.push_back(DisplayRun{std::move(piece), pack(span_fg(t, s)), is_bold(s.style)});
    }
    out_width = col;

    const int wc = (wrap && wrap_cols >= 1) ? wrap_cols : 0;
    auto vlines = wrap_display_runs(runs, wc);

    auto lines = std::make_shared<slint::VectorModel<DiffLine>>();
    for (const auto& vl : vlines) {
        auto spans = std::make_shared<slint::VectorModel<DiffSpan>>();
        for (const auto& r : vl) {
            DiffSpan d;
            d.text = shared(r.text);
            d.color = unpack(r.argb);
            d.bold = r.bold;
            spans->push_back(d);
        }
        DiffLine dl;
        dl.spans = spans;
        lines->push_back(dl);
    }
    return lines;
}

}  // namespace

GuiTheme
load_gui_theme(const std::string& theme_name) {
    diffy::ColumnViewCharacters chars;
    diffy::ColumnViewSettings view;
    diffy::ColumnViewTextStyle style;
    diffy::ColumnViewTextStyleEscapeCodes escapes;
    diffy::config_apply_theme(theme_name, chars, view, style, escapes);

    // Decide light/dark from the background luminance (only known for 24-bit).
    auto lum = [](const diffy::TermColor& c) -> int {
        if (c.kind == diffy::TermColor::Kind::Color24bit) {
            return (2126 * c.r + 7152 * c.g + 722 * c.b) / 10000;
        }
        return -1;
    };
    const int L = lum(style.background.bg);
    const bool dark = (L >= 0) ? (L < 128) : true;

    const slint::Color def_bg = dark ? rgb(0x1e, 0x1e, 0x1e) : rgb(0xff, 0xff, 0xff);
    const slint::Color def_fg = dark ? rgb(0xd4, 0xd4, 0xd4) : rgb(0x24, 0x29, 0x2e);

    GuiTheme t;
    t.dark = dark;
    t.bg = to_color(style.background.bg, def_bg);
    t.panel_bg = t.bg;
    t.fg = to_color(style.common_line.fg, def_fg);
    t.gutter_fg = to_color(style.common_line_number.fg, rgb(0x6e, 0x77, 0x81));
    t.header_bg = to_color(style.header.bg, dark ? rgb(0x2d, 0x2d, 0x30) : rgb(0xdd, 0xf4, 0xff));
    t.header_fg = to_color(style.header.fg, dark ? rgb(0x9c, 0xdc, 0xfe) : rgb(0x09, 0x69, 0xda));
    t.accent = t.header_fg;
    t.divider = to_color(style.frame.fg, dark ? rgb(0x3c, 0x3c, 0x3c) : rgb(0xd0, 0xd7, 0xde));
    t.delete_bg = to_color(style.delete_line.bg, dark ? rgb(0x3a, 0x20, 0x20) : rgb(0xff, 0xeb, 0xe9));
    t.insert_bg = to_color(style.insert_line.bg, dark ? rgb(0x20, 0x36, 0x20) : rgb(0xe6, 0xff, 0xec));
    t.delete_token = to_color(style.delete_line_number.fg, dark ? rgb(0xf4, 0x87, 0x71) : rgb(0xcf, 0x22, 0x2e));
    t.insert_token = to_color(style.insert_line_number.fg, dark ? rgb(0x73, 0xc9, 0x91) : rgb(0x11, 0x63, 0x29));
    return t;
}

RowModel
build_row_model(const diffy::DiffViewModel& model, const GuiTheme& theme, int tab_width, bool wrap,
                int wrap_cols) {
    RowModel result;
    result.rows = std::make_shared<slint::VectorModel<DiffRowData>>();

    for (const auto& r : model.rows) {
        DiffRowData d;
        if (r.kind == diffy::RowKind::HunkHeader) {
            d.is_header = true;
            d.header = shared(r.header_text);
            d.left_present = false;
            d.right_present = false;
            d.left_lines = std::make_shared<slint::VectorModel<DiffLine>>();
            d.right_lines = std::make_shared<slint::VectorModel<DiffLine>>();
            result.rows->push_back(d);
            continue;
        }

        d.is_header = false;
        d.old_no = r.old_lineno ? shared(std::to_string(*r.old_lineno)) : slint::SharedString();
        d.new_no = r.new_lineno ? shared(std::to_string(*r.new_lineno)) : slint::SharedString();
        d.left_present = r.left.present;
        d.right_present = r.right.present;
        d.left_bg = cell_bg(theme, r.left);
        d.right_bg = cell_bg(theme, r.right);
        int lw = 0, rw = 0;
        d.left_lines = make_lines(theme, r.left, tab_width, wrap, wrap_cols, lw);
        d.right_lines = make_lines(theme, r.right, tab_width, wrap, wrap_cols, rw);
        d.left_cols = lw;
        d.right_cols = rw;
        result.max_cols = std::max(result.max_cols, std::max(lw, rw));
        result.rows->push_back(d);
    }

    return result;
}

}  // namespace diffy::gui
