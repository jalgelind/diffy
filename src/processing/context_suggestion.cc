#include "context_suggestion.hpp"

#include <unordered_set>

#include <fmt/format.h>

#include "config_parser/config_tokenizer.hpp"

using namespace diffy::config_tokenizer;

bool
diffy::context_find(std::vector<diffy::Line> lines, int from, ContextSuggestion* out_suggestions) {
    config_tokenizer::ParseOptions options;
    options.strip_spaces = true;
    options.strip_annotated_string_tokens = true;

    const auto& start_line = lines[from];
    const auto start_indent = start_line.indentation_level;
    const auto start_scope = start_line.scope_level;

    fmt::print("initial cursor position: line_idx: {}, indent={}, scope={}\n", from, start_indent, start_scope);

    auto find_parent_scope_by_indentation = [&](int from_line, int from_scope, int num_scopes){
        int indent_target = std::max(0, from_scope - num_scopes);
        
        int best = 0;
        int i = from_line;
        while (i >= 0 && lines[i].indentation_level > indent_target) {
            best = i;
            i--;
        }

        return (lines[best].indentation_level - indent_target) == num_scopes ? best : -1;
    };

    auto find_parent_scope_by_curly = [&](int from_line, int from_scope, int num_scopes){
        int scope_target = std::max(0, from_scope - num_scopes);
        
        int best = 0;
        int i = from_line;
        while (i >= 0 && lines[i].scope_level > scope_target) {
            best = i;
            i--;
        }

        return (lines[best].scope_level - scope_target) == num_scopes ? best : -1;
    };

    std::unordered_set<int> found;

    for (int i = 0; i < 4; i++) {
        int indent_parent = find_parent_scope_by_indentation(from, start_indent, i);
        int scope_parent = find_parent_scope_by_curly(from, start_scope, i);
        if (indent_parent != -1)
            found.insert(indent_parent);
        if (scope_parent != -1)
            found.insert(scope_parent);
        //fmt::print("i: {}, scope: {}, indent: {}\n", i, scope_parent, indent_parent);
    }

    fmt::print("suggested start positions:\n");
    for (auto suggested_pos : found) {
        fmt::print("    {}\n", suggested_pos);
    }

    if (found.empty()) {
        return false;
    }

    int start_pos = *found.begin();

    std::vector<config_tokenizer::Token> tokens;
    const int BACK_RANGE = 3; // in lines, but maybe it should be for symbols
    int ctx_start = std::max(0, start_pos-BACK_RANGE);
    int ctx_end = start_pos;

    std::string text;

    for (int i = ctx_start; i <= ctx_end; i++) {
        text.append(lines[i].line);
    }

    config_tokenizer::ParseResult result;
    if (config_tokenizer::tokenize(text, options, result)) {
        tokens = result.tokens;
    } else {
        return false;
    }

    config_tokenizer::token_dump(tokens, text);

    return false;
}