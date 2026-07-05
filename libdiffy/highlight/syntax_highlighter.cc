#include "highlight/syntax_highlighter.hpp"

#include "highlight/language_ts.hpp"
#include "util/binary_detect.hpp"

namespace diffy {

#ifdef DIFFY_ENABLE_HIGHLIGHT

}  // namespace diffy

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace diffy {

namespace {

// Byte offset of the start of each line, plus the buffer end, so line `i`
// spans [starts[i], starts[i+1]). Line length excludes the trailing newline.
std::vector<uint32_t>
line_starts(std::string_view src) {
    std::vector<uint32_t> starts;
    starts.push_back(0);
    for (uint32_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    starts.push_back(static_cast<uint32_t>(src.size()));
    return starts;
}

}  // namespace

LineHighlights
highlight_source(std::string_view source, Language lang) {
    LineHighlights empty;
    if (source.empty() || source.size() > kHighlightSizeCap || looks_binary(source)) {
        return empty;
    }
    const TSLanguage* ts_lang = ts_language_for(lang);
    if (!ts_lang) {
        return empty;
    }
    const std::string query_src = highlight_query_for(lang);
    if (query_src.empty()) {
        return empty;
    }

    uint32_t err_offset = 0;
    TSQueryError err_type = TSQueryErrorNone;
    TSQuery* query = ts_query_new(ts_lang, query_src.c_str(),
                                  static_cast<uint32_t>(query_src.size()), &err_offset, &err_type);
    if (!query) {
        return empty;  // malformed query for this grammar version — skip gracefully
    }

    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, ts_lang)) {
        // Grammar built for an incompatible language ABI — degrade to no-op.
        ts_parser_delete(parser);
        ts_query_delete(query);
        return empty;
    }
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.data(),
                                          static_cast<uint32_t>(source.size()));

    // Per-line, per-byte group (uint8_t); 0 == None.
    const std::vector<uint32_t> starts = line_starts(source);
    const size_t nlines = starts.size() >= 1 ? starts.size() - 1 : 0;
    std::vector<std::vector<uint8_t>> paint(nlines);
    auto line_len = [&](size_t row) -> uint32_t {
        if (row + 1 >= starts.size()) {
            return 0;
        }
        uint32_t len = starts[row + 1] - starts[row];
        // Drop the trailing '\n' (and a preceding '\r') from the paintable width.
        while (len > 0 && (source[starts[row] + len - 1] == '\n' ||
                           source[starts[row] + len - 1] == '\r')) {
            --len;
        }
        return len;
    };

    // Collect captures, then paint shortest-range-last so the innermost capture
    // wins — a good approximation of tree-sitter's precedence rules.
    struct Cap {
        TSPoint start, end;
        uint32_t len;
        HighlightGroup group;
    };
    std::vector<Cap> caps;
    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));
    TSQueryMatch match;
    uint32_t capture_index = 0;
    while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
        const TSQueryCapture& c = match.captures[capture_index];
        uint32_t name_len = 0;
        const char* name = ts_query_capture_name_for_id(query, c.index, &name_len);
        HighlightGroup g = group_for_capture(std::string_view(name, name_len));
        if (g == HighlightGroup::None) {
            continue;
        }
        const uint32_t sb = ts_node_start_byte(c.node);
        const uint32_t eb = ts_node_end_byte(c.node);
        caps.push_back({ts_node_start_point(c.node), ts_node_end_point(c.node), eb - sb, g});
    }
    std::stable_sort(caps.begin(), caps.end(),
                     [](const Cap& a, const Cap& b) { return a.len > b.len; });

    auto ensure = [&](size_t row) -> std::vector<uint8_t>& {
        if (paint[row].empty()) {
            paint[row].assign(line_len(row), 0);
        }
        return paint[row];
    };
    auto paint_row = [&](size_t row, uint32_t from, uint32_t to, HighlightGroup g) {
        if (row >= nlines) {
            return;
        }
        auto& v = ensure(row);
        to = std::min<uint32_t>(to, static_cast<uint32_t>(v.size()));
        for (uint32_t i = from; i < to; ++i) {
            v[i] = static_cast<uint8_t>(g);
        }
    };

    for (const auto& cap : caps) {
        if (cap.start.row == cap.end.row) {
            paint_row(cap.start.row, cap.start.column, cap.end.column, cap.group);
        } else {
            paint_row(cap.start.row, cap.start.column, line_len(cap.start.row), cap.group);
            for (uint32_t r = cap.start.row + 1; r < cap.end.row; ++r) {
                paint_row(r, 0, line_len(r), cap.group);
            }
            paint_row(cap.end.row, 0, cap.end.column, cap.group);
        }
    }

    // Collapse each line's per-byte groups into runs.
    LineHighlights result(nlines);
    for (size_t row = 0; row < nlines; ++row) {
        const auto& v = paint[row];
        uint32_t i = 0;
        while (i < v.size()) {
            if (v[i] == 0) {
                ++i;
                continue;
            }
            uint32_t j = i + 1;
            while (j < v.size() && v[j] == v[i]) {
                ++j;
            }
            result[row].push_back({i, j, static_cast<HighlightGroup>(v[i])});
            i = j;
        }
    }

    ts_query_cursor_delete(cursor);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    ts_query_delete(query);
    return result;
}

}  // namespace diffy

#else  // !DIFFY_ENABLE_HIGHLIGHT

LineHighlights
highlight_source(std::string_view, Language) {
    return {};
}

}  // namespace diffy

#endif
