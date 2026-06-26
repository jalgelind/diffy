#include "diff_bridge.hpp"

#include "highlight/highlight_palette.hpp"

#include <algorithm>
#include <string>

namespace diffy::gui {

namespace {

slint::SharedString
shared(const std::string& s) {
    return slint::SharedString(s.c_str());
}

struct Rgb {
    uint8_t r, g, b;
};

slint::Color
color(Rgb c) {
    return slint::Color::from_argb_uint8(255, c.r, c.g, c.b);
}

slint::Color
color_a(Rgb c, uint8_t a) {
    return slint::Color::from_argb_uint8(a, c.r, c.g, c.b);
}

// A small, crisp palette. Theme-file colour reuse (16-colour palette -> RGB) is a
// future enhancement; this gives a clean, readable default for both variants.
struct Palette {
    Rgb common_fg;
    Rgb delete_fg;
    Rgb delete_token;
    Rgb insert_fg;
    Rgb insert_token;
    Rgb delete_bg;
    Rgb insert_bg;
};

Palette
palette_for(const diffy::GuiSettings& settings) {
    const bool light = settings.theme_variant == "light";
    if (light) {
        return Palette{
            {0x24, 0x29, 0x2e}, {0x8b, 0x2f, 0x2f}, {0xc0, 0x2a, 0x2a},
            {0x1f, 0x6f, 0x2f}, {0x18, 0x82, 0x3a}, {0xff, 0xe0, 0xe0}, {0xdc, 0xff, 0xdc},
        };
    }
    return Palette{
        {0xd4, 0xd4, 0xd4}, {0xc9, 0xa0, 0xa0}, {0xf4, 0x87, 0x71},
        {0xa0, 0xc9, 0xa0}, {0x73, 0xc9, 0x91}, {0xff, 0x33, 0x33}, {0x33, 0xff, 0x33},
    };
}

slint::Color
span_color(const Palette& p, diffy::SpanStyle style) {
    switch (style) {
        case diffy::SpanStyle::Delete:
            return color(p.delete_fg);
        case diffy::SpanStyle::DeleteToken:
            return color(p.delete_token);
        case diffy::SpanStyle::Insert:
            return color(p.insert_fg);
        case diffy::SpanStyle::InsertToken:
            return color(p.insert_token);
        case diffy::SpanStyle::Common:
        case diffy::SpanStyle::Meta:
        default:
            return color(p.common_fg);
    }
}

bool
is_bold(diffy::SpanStyle style) {
    return style == diffy::SpanStyle::DeleteToken || style == diffy::SpanStyle::InsertToken;
}

slint::Color
cell_bg(const Palette& p, const diffy::DiffCell& cell) {
    if (!cell.present) {
        return slint::Color::from_argb_uint8(0, 0, 0, 0);
    }
    switch (cell.type) {
        case diffy::EditType::Delete:
            return color_a(p.delete_bg, 28);
        case diffy::EditType::Insert:
            return color_a(p.insert_bg, 28);
        default:
            return slint::Color::from_argb_uint8(0, 0, 0, 0);
    }
}

// Slint's Text has no glyph for a literal tab or other control characters, so
// they render as "tofu" boxes. Expand tabs to the next tab stop and turn any
// remaining control characters into a space. `col` tracks the running display
// column across the spans of a line so tab stops line up. UTF-8 continuation
// bytes (0x80-0xBF) are copied without advancing the column, so a multi-byte
// codepoint counts as one column.
std::string
sanitize(const std::string& in, int tab_width, int& col) {
    std::string out;
    out.reserve(in.size());
    if (tab_width < 1) {
        tab_width = 1;
    }
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '\t') {
            int n = tab_width - (col % tab_width);
            out.append(static_cast<size_t>(n), ' ');
            col += n;
            ++i;
        } else if (c < 0x20 || c == 0x7f) {
            out.push_back(' ');
            ++col;
            ++i;
        } else {
            out.push_back(static_cast<char>(c));
            ++col;  // count the leading byte of the codepoint
            ++i;
            while (i < in.size() && (static_cast<unsigned char>(in[i]) & 0xc0) == 0x80) {
                out.push_back(in[i]);
                ++i;
            }
        }
    }
    return out;
}

// Foreground colour for a span: the tree-sitter syntax colour when the span is
// highlighted, otherwise the diff-semantic colour. The diff background tint and
// the bold emphasis on changed tokens are applied separately, so highlighted
// text still reads as added/removed.
slint::Color
span_fg(const Palette& p, const diffy::StyledSpan& s, bool light) {
    if (s.syntax != diffy::HighlightGroup::None) {
        const diffy::HlRgb c = diffy::syntax_color(s.syntax, light);
        return slint::Color::from_argb_uint8(255, c.r, c.g, c.b);
    }
    return span_color(p, s.style);
}

// Build the Slint span list for a cell and report its display width (columns).
std::shared_ptr<slint::VectorModel<DiffSpan>>
make_spans(const Palette& p, const diffy::DiffCell& cell, int tab_width, bool light, int& out_width) {
    auto spans = std::make_shared<slint::VectorModel<DiffSpan>>();
    int col = 0;
    for (const auto& s : cell.spans) {
        DiffSpan d;
        d.text = shared(sanitize(s.text, tab_width, col));
        d.color = span_fg(p, s, light);
        d.bold = is_bold(s.style);
        spans->push_back(d);
    }
    out_width = col;
    return spans;
}

}  // namespace

RowModel
build_row_model(const diffy::DiffViewModel& model, const diffy::GuiSettings& settings) {
    const Palette p = palette_for(settings);
    const bool light = settings.theme_variant == "light";
    const int tab_width = static_cast<int>(settings.tab_width);
    RowModel result;
    result.rows = std::make_shared<slint::VectorModel<DiffRowData>>();

    for (const auto& r : model.rows) {
        DiffRowData d;
        if (r.kind == diffy::RowKind::HunkHeader) {
            d.is_header = true;
            d.header = shared(r.header_text);
            d.left_present = false;
            d.right_present = false;
            d.left_spans = std::make_shared<slint::VectorModel<DiffSpan>>();
            d.right_spans = std::make_shared<slint::VectorModel<DiffSpan>>();
            result.rows->push_back(d);
            continue;
        }

        d.is_header = false;
        d.old_no = r.old_lineno ? shared(std::to_string(*r.old_lineno)) : slint::SharedString();
        d.new_no = r.new_lineno ? shared(std::to_string(*r.new_lineno)) : slint::SharedString();
        d.left_present = r.left.present;
        d.right_present = r.right.present;
        d.left_bg = cell_bg(p, r.left);
        d.right_bg = cell_bg(p, r.right);
        int lw = 0, rw = 0;
        d.left_spans = make_spans(p, r.left, tab_width, light, lw);
        d.right_spans = make_spans(p, r.right, tab_width, light, rw);
        result.max_cols = std::max(result.max_cols, std::max(lw, rw));
        result.rows->push_back(d);
    }

    return result;
}

}  // namespace diffy::gui
