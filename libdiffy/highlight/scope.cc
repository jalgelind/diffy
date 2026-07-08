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
