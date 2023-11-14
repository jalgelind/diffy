#include "context_suggestion.hpp"

#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include "config_parser/config_tokenizer.hpp"

using namespace diffy::config_tokenizer;

//#define LOCAL_DEBUG

bool
diffy::context_find(gsl::span<diffy::Line> lines, int from, std::vector<Suggestion>& out_suggestions) {
    config_tokenizer::ParseOptions options;
    options.strip_spaces = false;
    options.strip_newlines = false;
    options.strip_annotated_string_tokens = true;
    options.strip_comments = true;

    if (from > lines.size()) {
        return false;
    }
#ifdef LOCAL_DEBUG
    fmt::print("linecount: {}, cursor: {}\n", lines.size(), from);
#endif

    const auto& start_line = lines[from];
    const auto start_indent = start_line.indentation_level;
    const auto start_scope = start_line.scope_level;

#ifdef LOCAL_DEBUG
    fmt::print("initial cursor position: line_idx: {}, indent={}, scope={}\n", from, start_indent, start_scope);
#endif

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
    }

#ifdef LOCAL_DEBUG
    fmt::print("suggested start positions:\n");
    for (auto suggested_pos : found) {
        fmt::print("    {}\n", suggested_pos);
    }
#endif

    if (found.empty()) {
        return false;
    }

    int start_pos = *found.begin();

    std::vector<config_tokenizer::Token> tokens;
    const int BACK_RANGE = 10; // in lines, but maybe it should be for symbols
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

#ifdef LOCAL_DEBUG
    config_tokenizer::token_dump(tokens, text);
#endif

    // Consider having separate filter functions for each language type
    // Each language type could be handled in a state-machine DSL-thing
    // like in the config parser.

    // TODO: We fail when context looks like: for (int i = 0; i < lines.size(); i++) {
    // We scan from { to ;, but miss that it's separators for the loop conditions...

    /*
        loop_score = 1
        function_score = 2

        // -1: any number of tokens ahead of identiifer
        //  0: no tokens ahead of identifier
        find(-1, TokenId_Identifier,
              0, TokenId_OpenParam,
             -1, TokenId_CloseParam,
              0, TokenId_StartCurly)
        // but we need to seek backwards
        find( 0, TokenId_StartCurly,
             -1, TokenId_CloseParam,
              0, TokenId_OpenParam,
             -1, TokenId_Identifier)
    */

    auto cxx_filter = [](std::vector<Token> tokens, int start) -> std::vector<Token> {
        std::vector<Token> result;

        int curly_end_pos = -1;
        int semi_start_pos = -1;
        for (int i = start; i >= 0; i--) {
            if (tokens[i].id & TokenId_OpenCurly) {
                curly_end_pos = i+1;
            } else if (curly_end_pos > 0 && tokens[i].id & TokenId_Semicolon) {
                semi_start_pos = i+1;
            }

            if (curly_end_pos != -1 && semi_start_pos != -1)
                break;
        }

#ifdef LOCAL_DEBUG
        fmt::print("curly_end_pos: {}\n", curly_end_pos);
        fmt::print("semi_start_pos: {}\n", semi_start_pos);
#endif

        if (curly_end_pos != -1 && semi_start_pos != -1) {
            for (int i = semi_start_pos; i < curly_end_pos; i++) {
                result.push_back(tokens[i]);
            }
        }
    
        return result;
    };

    std::unordered_map<std::string, std::function<std::vector<Token>(std::vector<Token>, int start)>> lang_filters = {
        { "cpp", cxx_filter }, { "cxx", cxx_filter }, { "cc", cxx_filter }, { "c", cxx_filter },
        { "hpp", cxx_filter }, { "hxx", cxx_filter }, { "h", cxx_filter },
    };

    auto& lang_filter = lang_filters.at("cc");

    auto filtered_tokens = lang_filter(tokens, tokens.size()-1);

    bool drop_newlines = true;
    bool drop_spaces = true;
    bool drop_curlies = true;
    std::string filtered_text;
    for (int i = 0; i < filtered_tokens.size(); i++) {
        if (filtered_tokens[i].id & TokenId_CloseCurly) {
            if (!drop_curlies)
                filtered_text += "}";
        } else if (filtered_tokens[i].id & TokenId_Identifier) {
            filtered_text += filtered_tokens[i].str_from(text);
            drop_curlies = false;
            drop_newlines = false;
            drop_spaces = false;
        } else if (filtered_tokens[i].id & TokenId_Newline) {
            if (!drop_newlines)
                filtered_text += " ";
            drop_spaces = true;
        } else if (filtered_tokens[i].id & TokenId_Space) {
            if (!drop_spaces)
                filtered_text += " ";
            drop_spaces = true;
        } else {
            filtered_text += filtered_tokens[i].str_from(text);
            drop_newlines = false;
            drop_spaces = false;
        }
    }
#ifdef LOCAL_DEBUG
    config_tokenizer::token_dump(filtered_tokens, text);
    fmt::print("Context: '{}'\n", filtered_text);
#endif

    // TODO: we should take the index of the first (or last?) token we are returning, not ctx_start wher
    out_suggestions.push_back({ctx_start, filtered_text});

    return true;
}