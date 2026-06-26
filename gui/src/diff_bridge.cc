#include "diff_bridge.hpp"

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

std::shared_ptr<slint::VectorModel<DiffSpan>>
make_spans(const Palette& p, const diffy::DiffCell& cell) {
    auto spans = std::make_shared<slint::VectorModel<DiffSpan>>();
    for (const auto& s : cell.spans) {
        DiffSpan d;
        d.text = shared(s.text);
        d.color = span_color(p, s.style);
        d.bold = is_bold(s.style);
        spans->push_back(d);
    }
    return spans;
}

}  // namespace

std::shared_ptr<slint::VectorModel<DiffRowData>>
build_row_model(const diffy::DiffViewModel& model, const diffy::GuiSettings& settings) {
    const Palette p = palette_for(settings);
    auto rows = std::make_shared<slint::VectorModel<DiffRowData>>();

    for (const auto& r : model.rows) {
        DiffRowData d;
        if (r.kind == diffy::RowKind::HunkHeader) {
            d.is_header = true;
            d.header = shared(r.header_text);
            d.left_present = false;
            d.right_present = false;
            d.left_spans = std::make_shared<slint::VectorModel<DiffSpan>>();
            d.right_spans = std::make_shared<slint::VectorModel<DiffSpan>>();
            rows->push_back(d);
            continue;
        }

        d.is_header = false;
        d.old_no = r.old_lineno ? shared(std::to_string(*r.old_lineno)) : slint::SharedString();
        d.new_no = r.new_lineno ? shared(std::to_string(*r.new_lineno)) : slint::SharedString();
        d.left_present = r.left.present;
        d.right_present = r.right.present;
        d.left_bg = cell_bg(p, r.left);
        d.right_bg = cell_bg(p, r.right);
        d.left_spans = make_spans(p, r.left);
        d.right_spans = make_spans(p, r.right);
        rows->push_back(d);
    }

    return rows;
}

}  // namespace diffy::gui
