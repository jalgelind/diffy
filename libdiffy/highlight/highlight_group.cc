#include "highlight/highlight_group.hpp"

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

    return HighlightGroup::None;
}

}  // namespace diffy
