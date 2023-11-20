#include "config_tokenizer_matcher.hpp"
#include "config_tokenizer.hpp"

#include <fmt/format.h>

using namespace diffy;
using namespace config_tokenizer;

namespace {
    // Duplicated from diffy suggestion engine
    std::string
    render_sequence(const std::vector<Token> tokens, const std::string& token_text) {
        bool drop_newlines = true;
        bool drop_spaces = true;
        bool drop_curlies = true;
        std::string text;
        for (int i = 0; i < tokens.size(); i++) {
            if (tokens[i].id & TokenId_CloseCurly) {
                if (!drop_curlies)
                    text += "}";
            } else if (tokens[i].id & TokenId_Identifier) {
                text += tokens[i].str_from(token_text);
                drop_curlies = false;
                drop_newlines = false;
                drop_spaces = false;
            } else if (tokens[i].id & TokenId_Newline) {
                if (!drop_newlines)
                    text += " ";
                drop_spaces = true;
            } else if (tokens[i].id & TokenId_Space) {
                if (!drop_spaces)
                    text += " ";
                drop_spaces = true;
            } else {
                text += tokens[i].str_from(token_text);
                drop_newlines = false;
                drop_spaces = false;
            }
        }
        return text;
    }
}

std::string
config_tokenizer::repr(SequencePoint p) {
    return fmt::format("SequencePoint[{}, {}]", repr(p.id), p.ident_match);
};

bool
config_tokenizer::token_match(const Token& token, const std::string& token_text, const SequencePoint& point) {
    if (token.id & point.id) {
        if (point.id & TokenId_Identifier && !point.ident_match.empty()) {
            return token.str_from(token_text) == point.ident_match;
        }
        return true;
    }
    return false;
};

// Scan `input` backwards and find the sequence pattern.
bool
config_tokenizer::reverse_find_sequence(std::vector<Token>& input, const std::string& input_text, std::vector<SequencePoint> sequence, SequenceMatch* result) {
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
        Token token = input[input_cursor];

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
            repr(token, text));
        fmt::print("               - {}\n", repr(p));
#endif
        switch (state) {
            case Scan: {
                if (token_match(token, input_text, p)) {
                    state = Matching;
                    match_end = input_cursor+1;
                } else {
                    input_cursor--;
                }
            } break;
            case Matching: {
                bool match_token = false;
                if (p.id == TokenId_Any) {
                    bool match_next_token = token_match(token, input_text, p_next);
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
                    match_token = token_match(token, input_text, p);
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