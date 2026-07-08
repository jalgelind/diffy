#include "highlight/highlight_group.hpp"

#include <utility>

namespace diffy {

namespace {

// Does `capture` start with `prefix` as a whole dotted component? i.e. prefix
// equals capture, or capture continues with a '.' right after prefix.
bool
has_prefix(std::string_view capture, std::string_view prefix) {
    if (capture.size() < prefix.size() || capture.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    return capture.size() == prefix.size() || capture[prefix.size()] == '.';
}

// Canonical config-key name per group, for the theme [syntax] table and user
// overrides. Flat identifiers (no dots — those are nesting in the config), so the
// builtin variants use an underscore. Keep in sync with the HighlightGroup enum.
constexpr std::pair<HighlightGroup, std::string_view> kGroupNames[] = {
    {HighlightGroup::None, "none"},
    {HighlightGroup::Comment, "comment"},
    {HighlightGroup::Keyword, "keyword"},
    {HighlightGroup::Operator, "operator"},
    {HighlightGroup::Punctuation, "punctuation"},
    {HighlightGroup::String, "string"},
    {HighlightGroup::Escape, "escape"},
    {HighlightGroup::Number, "number"},
    {HighlightGroup::Boolean, "boolean"},
    {HighlightGroup::Constant, "constant"},
    {HighlightGroup::ConstantBuiltin, "constant_builtin"},
    {HighlightGroup::Function, "function"},
    {HighlightGroup::Method, "method"},
    {HighlightGroup::Constructor, "constructor"},
    {HighlightGroup::Type, "type"},
    {HighlightGroup::TypeBuiltin, "type_builtin"},
    {HighlightGroup::Variable, "variable"},
    {HighlightGroup::Parameter, "parameter"},
    {HighlightGroup::Property, "property"},
    {HighlightGroup::Namespace, "namespace"},
    {HighlightGroup::Label, "label"},
    {HighlightGroup::Tag, "tag"},
    {HighlightGroup::Attribute, "attribute"},
};

}  // namespace

HighlightGroup
group_for_capture(std::string_view c) {
    // Order matters: check the most specific names before their parents so that
    // e.g. "function.method" wins over "function" and "type.builtin" over
    // "type". Names follow the de-facto tree-sitter highlight taxonomy shared by
    // most grammars / editors.

    if (has_prefix(c, "comment")) return HighlightGroup::Comment;

    // strings & their escapes
    if (has_prefix(c, "escape") || has_prefix(c, "string.escape") ||
        has_prefix(c, "string.special"))
        return HighlightGroup::Escape;
    if (has_prefix(c, "string") || has_prefix(c, "character")) return HighlightGroup::String;

    // literals
    if (has_prefix(c, "boolean")) return HighlightGroup::Boolean;
    if (has_prefix(c, "number") || has_prefix(c, "float") || has_prefix(c, "integer"))
        return HighlightGroup::Number;
    if (has_prefix(c, "constant.builtin")) return HighlightGroup::ConstantBuiltin;
    if (has_prefix(c, "constant")) return HighlightGroup::Constant;

    // callables
    if (has_prefix(c, "constructor")) return HighlightGroup::Constructor;
    if (has_prefix(c, "function.method") || has_prefix(c, "method"))
        return HighlightGroup::Method;
    if (has_prefix(c, "function")) return HighlightGroup::Function;  // incl. function.builtin

    // types
    if (has_prefix(c, "type.builtin")) return HighlightGroup::TypeBuiltin;
    if (has_prefix(c, "type") || has_prefix(c, "class") || has_prefix(c, "struct"))
        return HighlightGroup::Type;

    // identifiers
    if (has_prefix(c, "variable.parameter") || has_prefix(c, "parameter"))
        return HighlightGroup::Parameter;
    if (has_prefix(c, "variable.member") || has_prefix(c, "property") || has_prefix(c, "field"))
        return HighlightGroup::Property;
    if (has_prefix(c, "namespace") || has_prefix(c, "module") || has_prefix(c, "package"))
        return HighlightGroup::Namespace;
    if (has_prefix(c, "variable.builtin")) return HighlightGroup::ConstantBuiltin;
    if (has_prefix(c, "variable")) return HighlightGroup::Variable;

    // keywords & operators
    if (has_prefix(c, "keyword")) return HighlightGroup::Keyword;
    if (has_prefix(c, "conditional") || has_prefix(c, "repeat") || has_prefix(c, "include") ||
        has_prefix(c, "exception"))
        return HighlightGroup::Keyword;
    if (has_prefix(c, "operator")) return HighlightGroup::Operator;
    if (has_prefix(c, "punctuation")) return HighlightGroup::Punctuation;

    // markup
    if (has_prefix(c, "tag")) return HighlightGroup::Tag;
    if (has_prefix(c, "attribute")) return HighlightGroup::Attribute;
    if (has_prefix(c, "label")) return HighlightGroup::Label;

    // Prose / markup scopes (markdown & friends emit text.* / markup.*). There are
    // no dedicated groups, so map to the nearest code groups — otherwise markdown
    // renders almost uncoloured: headings as keywords, inline/block code as
    // strings, links & URIs as constants, block quotes as comments.
    if (has_prefix(c, "markup.heading") || has_prefix(c, "text.title")) return HighlightGroup::Keyword;
    if (has_prefix(c, "markup.raw") || has_prefix(c, "text.literal")) return HighlightGroup::String;
    if (has_prefix(c, "markup.link") || has_prefix(c, "text.uri") || has_prefix(c, "text.reference"))
        return HighlightGroup::Constant;
    if (has_prefix(c, "markup.quote") || has_prefix(c, "text.quote")) return HighlightGroup::Comment;

    return HighlightGroup::None;
}

std::string_view
highlight_group_name(HighlightGroup group) {
    for (const auto& [g, name] : kGroupNames)
        if (g == group)
            return name;
    return "none";
}

std::optional<HighlightGroup>
highlight_group_from_name(std::string_view name) {
    for (const auto& [g, n] : kGroupNames)
        if (n == name)
            return g;
    return std::nullopt;
}

}  // namespace diffy
