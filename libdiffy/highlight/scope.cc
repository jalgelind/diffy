#include "highlight/scope.hpp"

#include "highlight/language_ts.hpp"
#include "highlight/syntax_highlighter.hpp"  // kHighlightSizeCap

namespace diffy {

#ifdef DIFFY_ENABLE_HIGHLIGHT

}  // namespace diffy

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstring>

namespace diffy {

namespace {

// A node is a "scope" if its grammar type names a definition. Tree-sitter type
// names embed these words consistently across grammars (function_definition,
// function_item, method_declaration, class_specifier, class_definition,
// impl_item, namespace_definition, …), so a substring match generalises to all
// languages without a per-grammar table. Single-line matches (e.g. a
// function_type annotation) are filtered out by the caller's multi-line check.
bool
is_scope_type(const char* type) {
    // A declarator (C/C++ function_declarator) is a sub-part of a definition;
    // skip it so we match the whole definition, including its return type, and
    // so body changes resolve to the same enclosing label as signature changes.
    if (std::strstr(type, "declarator") != nullptr) {
        return false;
    }
    static const char* const kw[] = {
        "function", "method",  "constructor", "class",     "struct", "impl",
        "namespace", "module", "interface",   "trait",     "enum",   "package",
        "subroutine", "procedure",
    };
    for (const char* k : kw) {
        if (std::strstr(type, k) != nullptr) {
            return true;
        }
    }
    return false;
}

// The definition's first line (its header), with leading indentation removed,
// internal whitespace collapsed, and truncated. The signature is assembled
// across lines (collapsing newlines to spaces) up to the body: the first '{'
// for brace languages, or a line-final ':' for Python `def`/`class`. This keeps
// the function name when the return type sits on its own line (e.g.
// "std::string\nfoo(...)") instead of stopping at just the type.
std::string
header_label(std::string_view src, uint32_t start_byte) {
    constexpr size_t kScan = 200;  // never walk more than this far for one label
    std::string out;
    bool pending_space = false;
    const size_t limit = std::min<size_t>(src.size(), start_byte + kScan);
    for (size_t i = start_byte; i < limit; ++i) {
        const char c = src[i];
        if (c == '{' || c == ';') {
            break;  // body / statement end
        }
        const bool ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (ws) {
            pending_space = !out.empty();  // drop leading; coalesce internal + newlines
            continue;
        }
        if (pending_space) {
            out += ' ';
            pending_space = false;
        }
        out += c;
        if (c == ':') {
            // Python `def f():` / `class C:` ends at a line-final colon. A C++
            // ':' (inheritance, bitfield) is followed by more on the same line,
            // so only stop when nothing but whitespace remains before a newline.
            size_t j = i + 1;
            while (j < src.size() && (src[j] == ' ' || src[j] == '\t' || src[j] == '\r')) {
                ++j;
            }
            if (j >= src.size() || src[j] == '\n') {
                break;
            }
        }
    }
    while (!out.empty() && (out.back() == '{' || out.back() == ':' || out.back() == ' ')) {
        out.pop_back();
    }

    constexpr size_t kMax = 80;
    if (out.size() > kMax) {
        out.resize(kMax - 3);
        out += "...";
    }
    return out;
}

// Text of a node, clamped to the source bounds.
std::string
node_text(TSNode n, std::string_view src) {
    const uint32_t a = ts_node_start_byte(n);
    const uint32_t b = ts_node_end_byte(n);
    if (a <= b && b <= src.size()) {
        return std::string(src.substr(a, b - a));
    }
    return {};
}

// Node types that name a definition (the identifier we'd jump to). Substrings
// aren't enough here — we want the exact leaf, so match whole type names.
bool
is_name_type(const char* t) {
    return std::strcmp(t, "identifier") == 0 || std::strcmp(t, "type_identifier") == 0 ||
           std::strcmp(t, "field_identifier") == 0 || std::strcmp(t, "constant") == 0 ||
           std::strcmp(t, "word") == 0;  // bash/cmake grammars call it "word"
}

// The defined identifier for a scope node. Prefers the grammar's "name" field
// (Python/Rust/Go/JS/… expose it); falls back to descending C/C++ "declarator"
// fields to reach the innermost identifier. A qualified name (Foo::bar) is
// reduced to its final component so a click on the unqualified use resolves.
std::string
definition_name(TSNode node, std::string_view src) {
    std::string out;
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name)) {
        out = node_text(name, src);
    } else {
        TSNode decl = ts_node_child_by_field_name(node, "declarator", 10);
        for (int guard = 0; !ts_node_is_null(decl) && guard < 8; ++guard) {
            const char* t = ts_node_type(decl);
            if (is_name_type(t) || std::strcmp(t, "qualified_identifier") == 0) {
                out = node_text(decl, src);
                break;
            }
            decl = ts_node_child_by_field_name(decl, "declarator", 10);
        }
    }
    if (const size_t p = out.rfind("::"); p != std::string::npos) {
        out = out.substr(p + 2);
    }
    // Reject anything that isn't a clean single identifier (defensive: keeps the
    // symbol table free of signatures/operators that slipped through).
    if (out.find_first_of(" \t\n(){}<>*&:.") != std::string::npos) {
        return {};
    }
    return out;
}

// A node that opens a lexical scope: a brace block ({...}) or any definition. Its
// row range is where a binding declared inside it is visible, so it's the unit for
// shadowing-correct local resolution (innermost wins).
bool
is_scope_boundary(const char* type) {
    return std::strcmp(type, "compound_statement") == 0 ||  // C/C++/JS/… block
           std::strcmp(type, "block") == 0 ||               // python/ruby/rust block
           is_scope_type(type);                             // function/class/lambda/…
}

// Collect the identifiers *bound* by a declaration node, descending only through
// `declarator` fields so initializers/values (which hold uses, not bindings) are
// never captured — `int x = y;` yields x, not y. Structured bindings (`auto [a,b]`)
// expose their names as plain children, so those are gathered directly.
void
collect_bound_idents(TSNode node, std::string_view src, std::vector<TSNode>& out) {
    if (std::strcmp(ts_node_type(node), "structured_binding_declarator") == 0) {
        const uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; ++i) {
            TSNode c = ts_node_child(node, i);
            if (is_name_type(ts_node_type(c))) {
                out.push_back(c);
            }
        }
        return;
    }
    const uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; ++i) {
        const char* fn = ts_node_field_name_for_child(node, i);
        if (!fn || std::strcmp(fn, "declarator") != 0) {
            continue;  // only follow the declarator chain, never the value/type
        }
        TSNode c = ts_node_child(node, i);
        if (is_name_type(ts_node_type(c))) {
            out.push_back(c);
        } else {
            collect_bound_idents(c, src, out);  // pointer/reference/array/init_declarator
        }
    }
}

// Node types whose `declarator` field(s) introduce local bindings. C/C++-focused:
// declarations, function parameters, range-for vars, and class members.
bool
is_binding_decl(const char* t) {
    return std::strcmp(t, "declaration") == 0 || std::strcmp(t, "parameter_declaration") == 0 ||
           std::strcmp(t, "optional_parameter_declaration") == 0 ||
           std::strcmp(t, "for_range_loop") == 0 || std::strcmp(t, "field_declaration") == 0;
}

}  // namespace

std::vector<CodeScope>
scope_outline(std::string_view source, Language lang) {
    std::vector<CodeScope> out;
    if (source.empty() || source.size() > kHighlightSizeCap) {
        return out;
    }
    // Binary guard (promised by the header): tree-sitter grammars are for source
    // text; a NUL byte means this isn't text, so skip it rather than parse garbage.
    if (source.find('\0') != std::string_view::npos) {
        return out;
    }
    const TSLanguage* ts_lang = ts_language_for(lang);
    if (!ts_lang) {
        return out;
    }

    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, ts_lang)) {
        ts_parser_delete(parser);
        return out;
    }
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.data(),
                                          static_cast<uint32_t>(source.size()));

    // Iterative pre-order walk over every node.
    std::vector<TSNode> stack;
    stack.push_back(ts_tree_root_node(tree));
    while (!stack.empty()) {
        TSNode node = stack.back();
        stack.pop_back();

        if (ts_node_is_named(node) && is_scope_type(ts_node_type(node))) {
            const TSPoint s = ts_node_start_point(node);
            const TSPoint e = ts_node_end_point(node);
            if (e.row > s.row) {  // definitions span multiple lines; skips type annotations
                std::string label = header_label(source, ts_node_start_byte(node));
                if (!label.empty()) {
                    std::string name = definition_name(node, source);
                    out.push_back({static_cast<int64_t>(s.row), static_cast<int64_t>(e.row),
                                   std::move(label), std::move(name)});
                }
            }
        }

        const uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; ++i) {
            stack.push_back(ts_node_child(node, i));
        }
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return out;
}

std::vector<LocalDef>
local_defs(std::string_view source, Language lang) {
    std::vector<LocalDef> out;
    if (source.empty() || source.size() > kHighlightSizeCap) {
        return out;
    }
    if (source.find('\0') != std::string_view::npos) {
        return out;
    }
    const TSLanguage* ts_lang = ts_language_for(lang);
    if (!ts_lang) {
        return out;
    }
    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, ts_lang)) {
        ts_parser_delete(parser);
        return out;
    }
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.data(),
                                          static_cast<uint32_t>(source.size()));
    const int64_t last_row = static_cast<int64_t>(ts_node_end_point(ts_tree_root_node(tree)).row);

    std::vector<TSNode> stack;
    stack.push_back(ts_tree_root_node(tree));
    while (!stack.empty()) {
        TSNode node = stack.back();
        stack.pop_back();

        if (is_binding_decl(ts_node_type(node))) {
            std::vector<TSNode> idents;
            collect_bound_idents(node, source, idents);
            for (TSNode id : idents) {
                std::string name = node_text(id, source);
                if (name.empty() ||
                    name.find_first_of(" \t\n(){}<>*&:.") != std::string::npos) {
                    continue;
                }
                // Visibility scope: the nearest enclosing block/definition.
                int64_t ss = 0, se = last_row;
                for (TSNode p = ts_node_parent(id); !ts_node_is_null(p); p = ts_node_parent(p)) {
                    if (is_scope_boundary(ts_node_type(p))) {
                        ss = static_cast<int64_t>(ts_node_start_point(p).row);
                        se = static_cast<int64_t>(ts_node_end_point(p).row);
                        break;
                    }
                }
                out.push_back({static_cast<int64_t>(ts_node_start_point(id).row), ss, se,
                               header_label(source, ts_node_start_byte(node)), std::move(name)});
            }
        }

        const uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; ++i) {
            stack.push_back(ts_node_child(node, i));
        }
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return out;
}

std::string
enclosing_scope(const std::vector<CodeScope>& outline, int64_t line) {
    const CodeScope* best = nullptr;
    for (const auto& s : outline) {
        if (line < s.start_line || line > s.end_line) {
            continue;
        }
        // Innermost == smallest containing range.
        if (!best || (s.end_line - s.start_line) < (best->end_line - best->start_line)) {
            best = &s;
        }
    }
    return best ? best->label : std::string();
}

}  // namespace diffy

#else  // !DIFFY_ENABLE_HIGHLIGHT

std::vector<CodeScope>
scope_outline(std::string_view, Language) {
    return {};
}

std::vector<LocalDef>
local_defs(std::string_view, Language) {
    return {};
}

std::string
enclosing_scope(const std::vector<CodeScope>&, int64_t) {
    return {};
}

}  // namespace diffy

#endif

// Pure (no tree-sitter): resolves a label from already-computed outlines.
namespace diffy {

std::string
hunk_context(const std::vector<CodeScope>& a_outline,
             const std::vector<CodeScope>& b_outline,
             int64_t a_change_line,
             int64_t b_change_line,
             int64_t a_start_line,
             int64_t b_start_line) {
    std::string ctx;
    if (b_change_line >= 0) {
        ctx = enclosing_scope(b_outline, b_change_line);
    }
    if (ctx.empty() && a_change_line >= 0) {
        ctx = enclosing_scope(a_outline, a_change_line);
    }
    if (ctx.empty()) {
        ctx = enclosing_scope(b_outline, b_start_line);
    }
    if (ctx.empty()) {
        ctx = enclosing_scope(a_outline, a_start_line);
    }
    return ctx;
}

std::optional<CodeScope>
resolve_definition(const std::vector<CodeScope>& outline, std::string_view name, int64_t near_line) {
    if (name.empty()) {
        return std::nullopt;
    }
    const CodeScope* best = nullptr;
    int64_t best_dist = 0;
    for (const auto& s : outline) {
        if (s.name != name) {
            continue;
        }
        const int64_t dist =
            near_line < 0 ? 0
                          : (s.start_line > near_line ? s.start_line - near_line
                                                      : near_line - s.start_line);
        if (!best || dist < best_dist) {
            best = &s;
            best_dist = dist;
        }
    }
    if (best) {
        return *best;
    }
    return std::nullopt;
}

std::optional<LocalDef>
resolve_local(const std::vector<LocalDef>& defs, std::string_view name, int64_t use_line) {
    if (name.empty()) {
        return std::nullopt;
    }
    const LocalDef* best = nullptr;
    for (const auto& d : defs) {
        if (d.name != name) {
            continue;
        }
        // Only bindings visible at the use site are eligible.
        if (use_line >= 0 && (use_line < d.scope_start || use_line > d.scope_end)) {
            continue;
        }
        if (!best) {
            best = &d;
            continue;
        }
        const int64_t d_span = d.scope_end - d.scope_start;
        const int64_t b_span = best->scope_end - best->scope_start;
        if (d_span != b_span) {
            if (d_span < b_span) {  // innermost (smallest) scope wins — shadowing
                best = &d;
            }
            continue;
        }
        // Same scope: prefer a declaration at/above the use, else the nearest one.
        if (use_line >= 0) {
            const bool d_before = d.line <= use_line, b_before = best->line <= use_line;
            const auto dist = [use_line](int64_t l) { return l > use_line ? l - use_line : use_line - l; };
            if (d_before != b_before) {
                if (d_before) {
                    best = &d;
                }
            } else if (dist(d.line) < dist(best->line)) {
                best = &d;
            }
        }
    }
    if (best) {
        return *best;
    }
    return std::nullopt;
}

}  // namespace diffy
