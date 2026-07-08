#include "highlight/highlight_group.hpp"

#include <doctest.h>

using namespace diffy;

TEST_CASE("highlight group name <-> enum resolver") {
    // Canonical names round-trip through the enum.
    for (auto g : {HighlightGroup::Comment, HighlightGroup::Keyword, HighlightGroup::Operator,
                   HighlightGroup::Punctuation, HighlightGroup::String, HighlightGroup::Escape,
                   HighlightGroup::Number, HighlightGroup::Boolean, HighlightGroup::Constant,
                   HighlightGroup::ConstantBuiltin, HighlightGroup::Function, HighlightGroup::Method,
                   HighlightGroup::Constructor, HighlightGroup::Type, HighlightGroup::TypeBuiltin,
                   HighlightGroup::Variable, HighlightGroup::Parameter, HighlightGroup::Property,
                   HighlightGroup::Namespace, HighlightGroup::Label, HighlightGroup::Tag,
                   HighlightGroup::Attribute}) {
        auto back = highlight_group_from_name(highlight_group_name(g));
        REQUIRE(back.has_value());
        CHECK(*back == g);
    }

    // A couple of specific keys as documented for the [syntax] table.
    CHECK(highlight_group_from_name("keyword") == HighlightGroup::Keyword);
    CHECK(highlight_group_from_name("type_builtin") == HighlightGroup::TypeBuiltin);
    CHECK(highlight_group_name(HighlightGroup::Comment) == "comment");

    // Unknown / empty names yield nothing (so the caller can warn and skip).
    CHECK_FALSE(highlight_group_from_name("bogus").has_value());
    CHECK_FALSE(highlight_group_from_name("").has_value());
    CHECK_FALSE(highlight_group_from_name("keyword.control").has_value());  // dotted != flat key
}

TEST_CASE("markdown / markup capture scopes map to the nearest code groups (ALG-13)") {
    CHECK(group_for_capture("text.title") == HighlightGroup::Keyword);
    CHECK(group_for_capture("markup.heading.1") == HighlightGroup::Keyword);
    CHECK(group_for_capture("text.literal") == HighlightGroup::String);
    CHECK(group_for_capture("markup.raw.block") == HighlightGroup::String);
    CHECK(group_for_capture("text.uri") == HighlightGroup::Constant);
    CHECK(group_for_capture("markup.link.url") == HighlightGroup::Constant);
    CHECK(group_for_capture("text.quote") == HighlightGroup::Comment);
    // Scopes we don't map still fall through to None.
    CHECK(group_for_capture("text.emphasis") == HighlightGroup::None);
}
