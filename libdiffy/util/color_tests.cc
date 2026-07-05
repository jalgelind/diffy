// Tests for color parsing. These pin the bg-only regression (A1) and the
// missing/mistyped `attr` crash (A5), and document hex/palette parsing.

#include "util/color.hpp"

#include <config_parser/config_parser.hpp>
#include <doctest.h>

using namespace diffy;

TEST_CASE("TermColor::parse_string") {
    SUBCASE("named palette color") {
        auto c = TermColor::parse_string("red");
        REQUIRE(c.has_value());
        CHECK(*c == TermColor::kRed);
    }
    SUBCASE("#RRGGBB") {
        auto c = TermColor::parse_string("#FF0000");
        REQUIRE(c.has_value());
        CHECK(c->kind == TermColor::Kind::Color24bit);
        CHECK(c->r == 255);
        CHECK(c->g == 0);
        CHECK(c->b == 0);
    }
    SUBCASE("#RGB expands each nibble") {
        auto c = TermColor::parse_string("#0F0");
        REQUIRE(c.has_value());
        CHECK(c->kind == TermColor::Kind::Color24bit);
        CHECK(c->r == 0);
        CHECK(c->g == 255);
        CHECK(c->b == 0);
    }
    SUBCASE("P<n> palette index") {
        auto c = TermColor::parse_string("P196");
        REQUIRE(c.has_value());
        CHECK(c->kind == TermColor::Kind::Color8bit);
        CHECK(c->r == 196);
    }
    SUBCASE("invalid inputs return nullopt") {
        CHECK_FALSE(TermColor::parse_string("").has_value());
        CHECK_FALSE(TermColor::parse_string("notacolor").has_value());
        CHECK_FALSE(TermColor::parse_string("#12").has_value());
        CHECK_FALSE(TermColor::parse_string("#GGGGGG").has_value());
    }
}

TEST_CASE("TermStyle::parse_value") {
    SUBCASE("bg-only table applies the background (regression A1)") {
        Value::Table t;
        t.insert("bg", Value{Value::String{"#0000FF"}});
        auto s = TermStyle::parse_value(t);
        REQUIRE(s.has_value());
        CHECK(s->bg == *TermColor::parse_hex("#0000FF"));
    }

    SUBCASE("missing attr does not crash (regression A5)") {
        Value::Table t;
        t.insert("fg", Value{Value::String{"red"}});
        auto s = TermStyle::parse_value(t);  // must not throw bad_variant_access
        REQUIRE(s.has_value());
        CHECK(s->fg == TermColor::kRed);
    }

    SUBCASE("attr of the wrong type does not crash") {
        Value::Table t;
        t.insert("attr", Value{Value::String{"bold"}});  // should be an array
        auto s = TermStyle::parse_value(t);              // must not throw
        REQUIRE(s.has_value());
    }

    SUBCASE("attr array is decoded") {
        Value::Array attrs;
        attrs.push_back(Value{Value::String{"bold"}});
        Value::Table t;
        t.insert("attr", Value{attrs});
        auto s = TermStyle::parse_value(t);
        REQUIRE(s.has_value());
        CHECK((static_cast<uint16_t>(s->attr) & static_cast<uint16_t>(TermStyle::Attribute::Bold)) != 0);
    }
}

TEST_CASE("TermStyle::to_ansi") {
    SUBCASE("24-bit foreground emits an SGR sequence") {
        TermStyle s{*TermColor::parse_hex("#FF0000"), TermColor::kNone};
        CHECK(s.to_ansi().find("38;2;255;0;0") != std::string::npos);
    }
    SUBCASE("fully-ignored style emits nothing") {
        TermStyle s{TermColor::kNone, TermColor::kNone};
        CHECK(s.to_ansi().empty());
    }
}

TEST_CASE("TermColor::parse_string — palette index edge cases") {
    SUBCASE("lowercase p is accepted") {
        auto c = TermColor::parse_string("p52");
        REQUIRE(c.has_value());
        CHECK(c->kind == TermColor::Kind::Color8bit);
        CHECK(c->r == 52);
    }
    SUBCASE("max valid index") {
        auto c = TermColor::parse_string("P255");
        REQUIRE(c.has_value());
        CHECK(c->r == 255);
    }
    SUBCASE("out-of-range index is rejected (not silently truncated)") {
        CHECK_FALSE(TermColor::parse_string("P300").has_value());
    }
    SUBCASE("trailing junk is rejected") {
        CHECK_FALSE(TermColor::parse_string("P12x").has_value());
    }
    SUBCASE("missing index is rejected") {
        CHECK_FALSE(TermColor::parse_string("P").has_value());
    }
}

TEST_CASE("color_map_set defines an alias resolvable by parse_string") {
    color_map_set("diffy_test_alias", TermColor::kGreen);
    auto c = TermColor::parse_string("diffy_test_alias");
    REQUIRE(c.has_value());
    CHECK(*c == TermColor::kGreen);
}

TEST_CASE("TermStyle attributes all round-trip through to_value/parse_value") {
    Value::Array attrs;
    for (const char* name :
         {"bold", "dim", "italic", "underline", "blink", "inverse", "hidden", "strikethrough"})
        attrs.push_back(Value{Value::String{name}});
    Value::Table t;
    t.insert("attr", Value{attrs});

    auto s = TermStyle::parse_value(t);
    REQUIRE(s.has_value());
    // Serialize and re-parse: the attribute set must survive intact.
    auto s2 = TermStyle::parse_value(s->to_value().as_table());
    REQUIRE(s2.has_value());
    CHECK(static_cast<uint16_t>(s2->attr) == static_cast<uint16_t>(s->attr));

    using A = TermStyle::Attribute;
    for (A bit :
         {A::Bold, A::Dim, A::Italic, A::Underline, A::Blink, A::Inverse, A::Hidden, A::Strikethrough})
        CHECK((static_cast<uint16_t>(s->attr) & static_cast<uint16_t>(bit)) != 0);
}

TEST_CASE("TermStyle round-trips through to_value/parse_value") {
    TermStyle s{TermColor::kRed, TermColor::kBlue, TermStyle::Attribute::Bold};
    auto v = s.to_value();
    REQUIRE(v.is_table());
    auto parsed = TermStyle::parse_value(v.as_table());
    REQUIRE(parsed.has_value());
    CHECK(parsed->fg == TermColor::kRed);
    CHECK(parsed->bg == TermColor::kBlue);
    CHECK((static_cast<uint16_t>(parsed->attr) & static_cast<uint16_t>(TermStyle::Attribute::Bold)) != 0);
}
