#include "context_suggestion.hpp"

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

    auto find_parent_scope_by_indentation = [&](int from, int parent_scope){
        int found = -1;
        for (int i = from; i >=0; i--) {
            if (lines[i].indentation_level < parent_scope) {
                found = i;
                break;
            }
        }
        return found;
    };

    auto find_parent_scope_by_curly = [&](int from, int parent_scope){
        int found = -1;
        for (int i = from; i >=0; i--) {
            if (lines[i].scope_level < parent_scope) {
                found = i;
                break;
            }
        }
        return found;
    };

    int indent_parent = find_parent_scope_by_indentation(from, start_indent-1);
    int scope_parent = find_parent_scope_by_curly(from, start_scope-1);

#if 0
    std::vector<config_tokenizer::Token> tokens;
    int ctx_start = 0;
    int ctx_end = 0;

    for (int i = ctx_start; i < ctx_end; i++)
    {
        config_tokenizer::ParseResult result;
        if (config_tokenizer::tokenize(lines[i].line, options, result)) {
            for (auto& token : result.tokens) {
                tokens.push_back(token);
            }
        } else {
            return false;
        }
    }

    config_tokenizer::token_dump(tokens);
#endif
    return false;
}