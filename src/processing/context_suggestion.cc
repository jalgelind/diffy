#include "context_suggestion.hpp"

#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include "config_parser/config_tokenizer.hpp"
#include "config_parser/config_tokenizer_matcher.hpp"

using namespace diffy::config_tokenizer;

//#define LOCAL_DEBUG

struct MatchInfo {
    std::vector<Token> tokens;
    int indentation_level = -1;
    int scope_level = -1;
};

std::string
render_sequence(const MatchInfo& match, const std::string& token_text) {
    bool drop_newlines = true;
    bool drop_spaces = true;
    bool drop_curlies = true;
    std::string text;
    for (int i = 0; i < match.tokens.size(); i++) {
        if (match.tokens[i].id & TokenId_CloseCurly) {
            if (!drop_curlies)
                text += "}";
        } else if (match.tokens[i].id & TokenId_Identifier) {
            text += match.tokens[i].str_from(token_text);
            drop_curlies = false;
            drop_newlines = false;
            drop_spaces = false;
        } else if (match.tokens[i].id & TokenId_Newline) {
            if (!drop_newlines)
                text += " ";
            drop_spaces = true;
        } else if (match.tokens[i].id & TokenId_Space) {
            if (!drop_spaces)
                text += " ";
            drop_spaces = true;
        } else {
            text += match.tokens[i].str_from(token_text);
            drop_newlines = false;
            drop_spaces = false;
        }
    }
    //auto indent = std::string(' ', 4 * match.indentation_level);
    return indent = text;
}

bool
diffy::context_find(gsl::span<diffy::Line> lines, int from, std::vector<Suggestion>& out_suggestions) {
    config_tokenizer::ParseOptions options;
    options.strip_spaces = false;
    options.strip_newlines = false;
    options.strip_annotated_string_tokens = true;
    options.strip_comments = true;

    if (from >= lines.size()) {
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

#ifdef LOCAL_DEBUG
    fmt::print("8< - - - - - - - - - - - - - - -\n");
    fmt::print("{}\n", text);
    fmt::print("- - - - - - - - - - - - - - - >8\n");
#endif

    config_tokenizer::ParseResult parse_results;
    if (config_tokenizer::tokenize(text, options, parse_results)) {
        tokens = parse_results.tokens;
    } else {
        return false;
    }

    auto generic_filter = [&](std::vector<Token> tokens, int start) -> MatchInfo {
        MatchInfo result;

        //config_tokenizer::token_dump(tokens, text);

        std::vector<SequencePoint> for_loop_sequence {
            SequencePoint { TokenId_Identifier, "for" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_Semicolon },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_Semicolon },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        std::vector<SequencePoint> while_loop_sequence {
            SequencePoint { TokenId_Identifier, "while" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        std::vector<SequencePoint> if_cond_sequence {
            SequencePoint { TokenId_Identifier, "if" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        std::vector<SequencePoint> switch_cond_sequence {
            SequencePoint { TokenId_Identifier, "switch" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        std::vector<SequencePoint> switch_case_sequence {
            SequencePoint { TokenId_Identifier, "case" },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_Identifier, ":" },
        };

        std::vector<SequencePoint> typedef_sequence {
            SequencePoint { TokenId_Identifier, "typedef" },
            SequencePoint { TokenId_Identifier }, // enum, struct etc
            SequencePoint { TokenId_Identifier }, // name
            SequencePoint { TokenId_OpenCurly },
        };

        std::vector<SequencePoint> typedef2_sequence {
            SequencePoint { TokenId_Identifier, "typedef" },
            SequencePoint { TokenId_Identifier }, // enum, struct etc
            SequencePoint { TokenId_Identifier }, // name
            SequencePoint { TokenId_OpenCurly },
        };

        // fn (...) {
        std::vector<SequencePoint> fn_sequence {
            SequencePoint { TokenId_Identifier, "fn" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        // def ...:
        std::vector<SequencePoint> def_sequence {
            SequencePoint { TokenId_Identifier, "def" },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_Identifier, ":" }, // TODO: hm! TokenId_Colon
        };

        // Good enough.
        std::vector<SequencePoint> function_sequence {
            SequencePoint { TokenId_Identifier },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        // ?
       std::vector<SequencePoint> other_sequence {
            SequencePoint { TokenId_Semicolon },
            SequencePoint { TokenId_OpenCurly },
        };

        struct NamedSequencePoint {
            std::string name;
            std::vector<SequencePoint>& sp;
        };
        std::vector<NamedSequencePoint> test_pattern_sequences {
            {"loop/for"        , for_loop_sequence},
            {"typedef/1"       , typedef_sequence},
            {"typedef/2"       , typedef2_sequence},
            {"loop/while"      , while_loop_sequence},
            {"cond/if"         , if_cond_sequence},
            {"cond/case"       , switch_case_sequence},
            {"cond/switch"     , switch_cond_sequence},
            {"func/1"          , fn_sequence},
            {"func/2"          , def_sequence},
            {"func/0"          , function_sequence},
            {"misc/semicurl"   , other_sequence}
        };

        int match_start = -1;
        int match_end = -1;

        // TODO: retain indentation level?
        SequenceMatch match;
        for (auto& pattern : test_pattern_sequences) {
            if (reverse_find_sequence(tokens, text, pattern.sp, &match)) {
                // We should also get the indentation level and scope level of the match
                fmt::print("Found {} at pos: {}..{}\n", pattern.name, match.start, match.end);
                match_start = match.start;
                match_end = match.end;
                break;
            }
        }

#ifdef LOCAL_DEBUG
        fmt::print("match_start: {}\n", match_start);
        fmt::print("match_end: {}\n", match_end);
#endif

        if (match_start != -1 && match_end != -1) {
            for (int i = match_start; i < match_end; i++) {
                result.tokens.push_back(tokens[i]);
            }
        }

        if (result.tokens.size() > 0) {
            result.indentation_level = lines[tokens[0].line].indentation_level;
            result.scope_level = lines[tokens[0].line].indentation_level;
        }

        return result;
    };
#if 0
    std::unordered_map<std::string, std::function<std::vector<Token>(std::vector<Token>, int start)>> lang_filters = {
        { "cpp", generic_filter }, { "cxx", generic_filter }, { "cc", generic_filter }, { "c", generic_filter },
        { "hpp", generic_filter }, { "hxx", generic_filter }, { "h", generic_filter },
    };

    auto& lang_filter = lang_filters.at("cc");
    auto filtered_tokens = lang_filter(tokens, tokens.size()-1);
#endif
    auto filtered_tokens = generic_filter(tokens, tokens.size()-1);

    std::string filtered_text = render_sequence(filtered_tokens, text);

#ifdef LOCAL_DEBUG
    config_tokenizer::token_dump(filtered_tokens, text);
    fmt::print("Context: '{}'\n", filtered_text);
#endif

    // TODO: we should take the index of the first (or last?) token we are returning, not ctx_start wher
    out_suggestions.push_back({ctx_start, filtered_text});

    return true;
}