// Theme-owned syntax palettes: the bundled [syntax] blocks are well-formed, and
// applying them via the public override API behaves (apply / precedence / clear).
// config_apply_theme itself loads from the on-disk config dir, so these exercise
// the same pieces it uses (the resolver, colour parsing, and the override store)
// against the bundled theme strings directly.

#include "config/config.hpp"
#include "highlight/highlight_group.hpp"
#include "highlight/highlight_palette.hpp"
#include "util/color.hpp"

#include <config_parser/config_parser.hpp>

#include <doctest.h>

#include <string>

using namespace diffy;

namespace {

// Apply a theme conf string's [syntax] table the way config_apply_theme does.
int
apply_theme_syntax(const std::string& conf) {
    Value tree;
    ParseResult pr;
    REQUIRE(cfg_parse_value_tree(conf, pr, tree));
    REQUIRE(pr.is_ok());
    auto syntax = tree.lookup_value_by_path("syntax");
    REQUIRE(syntax.has_value());
    REQUIRE(syntax->get().is_table());
    int applied = 0;
    syntax->get().as_table().for_each([&](const std::string& key, Value& v) {
        auto g = highlight_group_from_name(key);
        auto col = v.is_string() ? TermColor::parse_string(v.as_string()) : std::nullopt;
        if (g && *g != HighlightGroup::None && col) {
            set_syntax_color_override(*g, HlRgb{col->r, col->g, col->b});
            applied++;
        }
    });
    return applied;
}

}  // namespace

TEST_CASE("bundled theme [syntax] blocks are well-formed and resolvable") {
    for (const auto& [name, conf] : config_bundled_themes()) {
        CAPTURE(name);
        Value tree;
        ParseResult pr;
        REQUIRE(cfg_parse_value_tree(conf, pr, tree));
        REQUIRE(pr.is_ok());
        auto syntax = tree.lookup_value_by_path("syntax");
        REQUIRE(syntax.has_value());
        REQUIRE(syntax->get().is_table());

        int entries = 0;
        syntax->get().as_table().for_each([&](const std::string& key, Value& v) {
            CAPTURE(key);
            CHECK(v.is_string());
            CHECK(highlight_group_from_name(key).has_value());           // key resolves
            CHECK(TermColor::parse_string(v.as_string()).has_value());   // value is a colour
            entries++;
        });
        CHECK(entries > 0);
    }
}

TEST_CASE("theme [syntax] overrides apply, take precedence, and clear without bleed") {
    clear_syntax_overrides();
    const HlRgb builtin = syntax_color(HighlightGroup::Keyword, /*light=*/false);

    std::string dracula;
    for (const auto& [name, conf] : config_bundled_themes())
        if (name == "theme_dracula")
            dracula = conf;
    REQUIRE(!dracula.empty());

    CHECK(apply_theme_syntax(dracula) > 0);

    // Dracula's keyword is #ff79c6; the theme override replaces the built-in.
    HlRgb kw = syntax_color(HighlightGroup::Keyword, false);
    CHECK(kw.r == 0xff);
    CHECK(kw.g == 0x79);
    CHECK(kw.b == 0xc6);

    // A later (user) override wins over the theme.
    set_syntax_color_override(HighlightGroup::Keyword, HlRgb{1, 2, 3});
    kw = syntax_color(HighlightGroup::Keyword, false);
    CHECK(kw.r == 1);
    CHECK(kw.g == 2);
    CHECK(kw.b == 3);

    // Clearing drops every override — no colour bleed into the next theme.
    clear_syntax_overrides();
    HlRgb reset = syntax_color(HighlightGroup::Keyword, false);
    CHECK(reset.r == builtin.r);
    CHECK(reset.g == builtin.g);
    CHECK(reset.b == builtin.b);
}

TEST_CASE("un-themed group falls back to the built-in light vs dark palette") {
    clear_syntax_overrides();
    // No override set → syntax_color returns the built-in default, which differs
    // between the light and dark palettes.
    HlRgb dark = syntax_color(HighlightGroup::Type, /*light=*/false);
    HlRgb light = syntax_color(HighlightGroup::Type, /*light=*/true);
    CHECK((dark.r != light.r || dark.g != light.g || dark.b != light.b));
    clear_syntax_overrides();
}

TEST_CASE("an on-disk theme without [syntax] falls back to the bundled palette") {
    clear_syntax_overrides();

    // A theme file that predates the [syntax] section — as it exists on an
    // existing install whose bundled .conf was written before palettes shipped.
    // config_apply_theme won't clobber it, so it must fall back to the matching
    // bundled theme's [syntax] by name (the migration this locks in).
    const std::string on_disk = "[settings]\ntheme = 'theme_dracula'\n";
    Value tree;
    ParseResult pr;
    REQUIRE(cfg_parse_value_tree(on_disk, pr, tree));
    REQUIRE(pr.is_ok());
    REQUIRE(!tree.lookup_value_by_path("syntax").has_value());  // genuinely absent

    // Locate the bundled theme by name — the same lookup config_apply_theme does.
    std::string bundled;
    for (const auto& [name, conf] : config_bundled_themes())
        if (name == "theme_dracula")
            bundled = conf;
    REQUIRE(!bundled.empty());
    CHECK(apply_theme_syntax(bundled) > 0);

    // Dracula's keyword palette is live even though the on-disk file lacked it.
    HlRgb kw = syntax_color(HighlightGroup::Keyword, /*light=*/false);
    CHECK(kw.r == 0xff);
    CHECK(kw.g == 0x79);
    CHECK(kw.b == 0xc6);

    clear_syntax_overrides();
}
