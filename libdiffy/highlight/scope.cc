#include "highlight/scope.hpp"

#include "highlight/language_ts.hpp"
#include "highlight/syntax_highlighter.hpp"  // kHighlightSizeCap

namespace diffy {

#ifdef DIFFY_ENABLE_HIGHLIGHT

}  // namespace diffy

#include <tree_sitter/api.h>

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
// internal whitespace collapsed, a trailing " {" stripped, and truncated.
std::string
header_label(std::string_view src, uint32_t start_byte) {
    uint32_t end = start_byte;
    while (end < src.size() && src[end] != '\n') {
        ++end;
    }
    std::string_view raw = src.substr(start_byte, end - start_byte);

    std::string out;
    out.reserve(raw.size());
    bool pending_space = false;
    for (char c : raw) {
        const bool ws = (c == ' ' || c == '\t' || c == '\r');
        if (ws) {
            pending_space = !out.empty();  // drop leading; coalesce internal
        } else {
            if (pending_space) {
                out += ' ';
                pending_space = false;
            }
            out += c;
        }
    }
    while (!out.empty() && (out.back() == '{' || out.back() == ' ')) {
        out.pop_back();
    }

    constexpr size_t kMax = 80;
    if (out.size() > kMax) {
        out.resize(kMax - 3);
        out += "...";
    }
    return out;
}

}  // namespace

std::vector<CodeScope>
scope_outline(std::string_view source, Language lang) {
    std::vector<CodeScope> out;
    if (source.empty() || source.size() > kHighlightSizeCap) {
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
                    out.push_back({static_cast<int64_t>(s.row), static_cast<int64_t>(e.row),
                                   std::move(label)});
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

}  // namespace diffy
