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

//#ifdef LOCAL_DEBUG
    fmt::print("8< - - - - - - - - - - - - - - -\n");
    fmt::print("{}\n", text);
    fmt::print("- - - - - - - - - - - - - - - >8\n");
//#endif

    config_tokenizer::ParseResult parse_results;
    if (config_tokenizer::tokenize(text, options, parse_results)) {
        tokens = parse_results.tokens;
    } else {
        return false;
    }

//#ifdef LOCAL_DEBUG
    config_tokenizer::token_dump(tokens, text);
//#endif

    struct SequencePoint {
        config_tokenizer::TokenId id;
        std::string ident_match;
    };

    struct SequenceMatch {
        int start;
        int end;
    };

    auto repr = [](SequencePoint p) {
        return fmt::format("SequencePoint[{}, {}]", config_tokenizer::repr(p.id), p.ident_match);
    };

    auto token_match = [&](const Token& token, const SequencePoint& point) -> bool {
        if (token.id & point.id) {
            if (point.id & TokenId_Identifier && !point.ident_match.empty()) {
                return token.str_from(text) == point.ident_match;
            }
            return true;
        }
        return false;
    };

    // Scan `input` backwards and find the sequence pattern.
    auto reverse_find_sequence = [&](std::vector<Token>& input, std::vector<SequencePoint> sequence, SequenceMatch* result) -> bool {
        int input_cursor = input.size() - 1;
        int seq_cursor = sequence.size() - 1;

        int match_start = -1;
        int match_end = -1;

        enum State : uint8_t {
            Scan     = 1 << 0,
            Matching = 1 << 1,
            Found    = 1 << 2,
            Abort    = 1 << 4,
        };
        const uint8_t ScanOrMatch = Scan | Matching;
        State state = Scan;

        while (state & ScanOrMatch && input_cursor >= 0 && seq_cursor >= 0) {
            Token& token = input[input_cursor];

            while (token.id & TokenId_Space) {
                if (input_cursor == 0) {
                    state = Abort;
                    break;
                }
                input_cursor--;
                token = input[input_cursor];
            }
            
            SequencePoint& p = sequence[seq_cursor];
            SequencePoint& p_next = sequence[std::max(0, seq_cursor - 1)];
#ifdef LOCAL_DEBUG
            fmt::print("[{} | {:2d} {:2d}] - {}\n",
                [&](){
                    switch(state) {
                        case Scan: return "SCAN";
                        case Matching: return "MTCH";
                        case Found: return "DONE";
                        case Abort: return "ABRT";
                    }
                    return "????";
                }(),
                input_cursor,
                seq_cursor,
                config_tokenizer::repr(token, text));
            fmt::print("               - {}\n", repr(p));
#endif
            switch (state) {
                case Scan: {
                    if (token_match(token, p)) {
                        state = Matching;
                        match_end = input_cursor+1;
                    } else {
                        input_cursor--;
                    }
                } break;
                case Matching: {
                    bool match_token = false;
                    if (p.id == TokenId_Any) {
                        bool match_next_token = token_match(token, p_next);
                        if (!match_next_token) {
                            // Scan input until we find the next token in the sequence
                            input_cursor--;
                            continue;
                        } else {
                            // Next token match the current sequence point
                            seq_cursor--;
                            match_token = true;
                        }
                    } else {
                        match_token = token_match(token, p);
                    }

                    if (match_token) {
                        if (seq_cursor == 0) {
                            state = Found;
                            match_start = input_cursor;
                            break;
                        }
                        
                        seq_cursor--;
                        input_cursor--;
                        continue;
                    }
                    // Match sequence broken. Try scanning again.
                    // TODO: adjust input cursor?
                    state = Scan;
                    seq_cursor = sequence.size() - 1;
                } break;
                case Found: {
#ifdef LOCAL_DEBUG
                    fmt::print("  Found match\n");
#endif
                } break;
                case Abort: {
#ifdef LOCAL_DEBUG
                    fmt::print("  Abort match\n");
#endif
                } break;
            }
        }

        if (match_start != -1 && match_end != -1) {
#if LOCAL_DEBUG
            fmt::print("Success - match start: {}, match_end: {}\n", match_start, match_end);
#endif
            if (result) {
                result->start = match_start;
                result->end = match_end;
            }
            return true;
        }
#if LOCAL_DEBUG
        fmt::print("Failure - match start: {}, match_end: {}\n", match_start, match_end);
#endif
        // Sequence not found
        return false;
    };
    auto cxx_filter = [&](std::vector<Token> tokens, int start) -> std::vector<Token> {
        std::vector<Token> result;

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

        int match_start = -1;
        int match_end = -1;

        SequenceMatch match;
        if (reverse_find_sequence(tokens, for_loop_sequence, &match)) {
            // We should also get the indentation level and scope level of the match
            fmt::print("Found for-loop at pos: {}..{}\n", match.start, match.end);
            match_start = match.start;
            match_end = match.end;
        } else {
            for (int i = start; i >= 0; i--) {
                if (tokens[i].id & TokenId_OpenCurly) {
                    match_end = i+1;
                } else if (match_end > 0 && tokens[i].id & TokenId_Semicolon) {
                    match_start = i+1;
                }

                if (match_end != -1 && match_start != -1)
                    break;
            }
        }

#ifdef LOCAL_DEBUG
        fmt::print("match_start: {}\n", match_start);
        fmt::print("match_end: {}\n", match_end);
#endif

        token_dump(tokens, text);

        if (match_start != -1 && match_end != -1) {
            for (int i = match_start; i < match_end; i++) {
                result.push_back(tokens[i]);
            }
        }

        token_dump(result, text);
    
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