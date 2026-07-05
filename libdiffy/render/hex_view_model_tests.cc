#include <doctest.h>

#include "binary/hex_align.hpp"
#include "render/hex_view_model.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace diffy;

namespace {

std::vector<uint8_t>
bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// Concatenate all span text of a cell.
std::string
cell_text(const DiffCell& c) {
    std::string s;
    for (const auto& sp : c.spans) {
        s += sp.text;
    }
    return s;
}

bool
has_style(const DiffCell& c, SpanStyle style) {
    for (const auto& sp : c.spans) {
        if (sp.style == style) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("build_hex_view side-by-side: changed bytes are token-styled, offsets embedded") {
    const auto a = bytes("The quick brown fox");
    const auto b = bytes("The quick red fox");
    const auto al = hex_align(a, b);

    DiffLayoutOptions opts;
    opts.mode = ViewMode::SideBySide;
    const auto model = build_hex_view(a, b, al, opts, 16, 3);

    CHECK(model.mode == ViewMode::SideBySide);
    REQUIRE_FALSE(model.rows.empty());

    bool saw_delete = false, saw_insert = false;
    for (const auto& r : model.rows) {
        if (r.kind != RowKind::Content) {
            continue;
        }
        // Offsets are embedded in the spans, not the line-number gutters.
        CHECK_FALSE(r.old_lineno.has_value());
        CHECK_FALSE(r.new_lineno.has_value());
        // No full-row tint; colour is per-span.
        CHECK(r.left.type == EditType::Common);
        CHECK(r.right.type == EditType::Common);
        saw_delete = saw_delete || has_style(r.left, SpanStyle::DeleteToken);
        saw_insert = saw_insert || has_style(r.right, SpanStyle::InsertToken);
    }
    CHECK(saw_delete);
    CHECK(saw_insert);

    // The ASCII gutter of the first row shows the shared prefix on both sides.
    const std::string left0 = cell_text(model.rows.front().left);
    CHECK(left0.find("The quick") != std::string::npos);
}

TEST_CASE("build_hex_view unified: uses -/+ prefixes and one column") {
    const auto a = bytes("abcdefghij");
    const auto b = bytes("abXdefghij");
    const auto al = hex_align(a, b);

    DiffLayoutOptions opts;
    opts.mode = ViewMode::Unified;
    const auto model = build_hex_view(a, b, al, opts, 16, 3);

    CHECK(model.mode == ViewMode::Unified);
    bool saw_minus = false, saw_plus = false;
    for (const auto& r : model.rows) {
        if (r.kind != RowKind::Content) {
            continue;
        }
        CHECK_FALSE(r.right.present);  // unified uses the left cell only
        const std::string t = cell_text(r.left);
        if (!t.empty() && t[0] == '-') saw_minus = true;
        if (!t.empty() && t[0] == '+') saw_plus = true;
    }
    CHECK(saw_minus);
    CHECK(saw_plus);
}

TEST_CASE("build_hex_view: identical input yields no change rows") {
    const auto a = bytes("identical bytes here");
    const auto al = hex_align(a, a);
    const auto model = build_hex_view(a, a, al, {}, 16, 3);
    for (const auto& r : model.rows) {
        if (r.kind == RowKind::Content) {
            CHECK_FALSE(has_style(r.left, SpanStyle::DeleteToken));
            CHECK_FALSE(has_style(r.right, SpanStyle::InsertToken));
        }
    }
}
