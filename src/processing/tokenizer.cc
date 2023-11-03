#include "tokenizer.hpp"

#include <fmt/format.h>
#include "util/hash.hpp"

#include <string>
#include <vector>

using namespace diffy;

namespace {

bool
is_delimiter(char c) {
    const char delimiters[] = ".,+-*/|(){}<>[]!\"'#$%^&*=:;";
    for (const auto delimiter : delimiters) {
        if (delimiter == c) {
            return true;
        }
    }
    return false;
}

std::string escape_special_characters(const std::string& input) {
    const char special_chars[] = {'\n', '\r', '\t'};
    const char* escape_sequences[] = {"\\n", "\\r", "\\t"};
    const size_t num_special_chars = sizeof(special_chars) / sizeof(special_chars[0]);

    std::string result;
    for (char c : input) {
        bool found = false;
        for (size_t i = 0; i < num_special_chars; ++i) {
            if (c == special_chars[i]) {
                result += escape_sequences[i];
                found = true;
                break;
            }
        }
        if (!found) {
            result += c;
        }
    }
    return result;
}

}  // namespace

std::string
diffy::repr(Token& token) {
    return fmt::format("Token(.start={:>3}, .length={:>3}, .flags={}, .hash={:8X})",
        token.start, token.length, token.flags, token.hash);
}

std::string
diffy::repr(Token& token, const std::string& source_text) {
    auto s = escape_special_characters(token.str_from(source_text));
    return fmt::format("Token(.start={:>3}, .length={:>3}, .flags={}, .text='{}')",
        token.start, token.length, token.flags, s);
}

bool
diffy::is_whitespace(char c) {
    const char whitespaces[] = " \t\r\n\f\v";
    for (const auto whitespace : whitespaces) {
        if (whitespace == c) {
            return true;
        }
    }
    return false;
}

bool
diffy::is_empty(const std::string& s) {
    for (char c : s) {
        if (!diffy::is_whitespace(c)) {
            return false;
        }
    }
    return true;
}

std::vector<Token>
diffy::tokenize(const std::string& text, std::vector<IndentRecord>* indentation_records) {
    std::vector<Token> result;

    // Nothing to do.
    if (text.size() == 0) {
        return result;
    }

    auto maxlen = text.size() - 1;
    std::size_t seeker = 0;

    std::size_t prev_line_token_index = 0;
    int prev_line_indentation = -1;
    int current_line = 1;
    int current_line_indentation = 0;
    int current_scope = 0;

    // Indentation with spaces should be distinct from tabs
    const auto INDENT_SIZE = 2u;

    do {
        auto start_idx = seeker;
        char c = text[start_idx];
        TokenFlag token_flags = TokenFlagNone;
        if (c == ' ') {
            while (seeker <= maxlen && text[seeker] == ' ') {
                seeker++;
            };
            token_flags = TokenFlagSpace;
            current_line_indentation += (seeker-start_idx) / INDENT_SIZE;
        } else if (c == '\t') {
            while (seeker <= maxlen && text[seeker] == '\t') {
                seeker++;
            };
            token_flags = TokenFlagTab;
            current_line_indentation += INDENT_SIZE;
        } else if (c == '\n') {

            // New line; register indentation level
            if (indentation_records && current_line_indentation != prev_line_indentation) {
                indentation_records->push_back(IndentRecord { current_line, current_line_indentation, current_scope, prev_line_token_index });
            }

            while (seeker <= maxlen && text[seeker] == '\n') {
                seeker++;
            };
            token_flags = TokenFlagLF;
            if (indentation_records) {
                prev_line_token_index = result.size(); // index of this item before it's appended.
                prev_line_indentation = current_line_indentation;
                current_line_indentation = 0;
                current_line++;
            }
        } else if (c == '\r') {
            while (seeker <= maxlen && text[seeker] == '\r') {
                seeker++;
            };
            token_flags = TokenFlagCR;
        } else if (c == '{' || c == '}') {
            int dir = c == '{' ? 1 : -1;
            while (seeker <= maxlen && text[seeker] == c) {
                current_scope += dir;
                seeker++;
            };
        } else if (is_delimiter(c)) {
            while (seeker <= maxlen && text[seeker] == c) {
                seeker++;
            };
        } else {
            while (seeker <= maxlen && !is_delimiter(text[seeker]) && !is_whitespace(text[seeker])) {
                seeker++;
            };
        }

        // Combine CR+LF into single token.
        bool combine_crlf =
            (token_flags & TokenFlagLF) && !result.empty() && result.back().flags & TokenFlagCR;
        if (combine_crlf) {
            auto cr = result.back();
            result.pop_back();
            auto new_length = cr.length + seeker - start_idx;
            auto hash = hash::hash(text.data() + cr.start, new_length);
            result.push_back({cr.start, new_length, hash, TokenFlagCRLF});
        } else {
            auto length = seeker - start_idx;
            auto hash = hash::hash(text.data() + start_idx, length);
            result.push_back({start_idx, length, hash, token_flags});
        }
    } while (seeker <= maxlen);

    // New line; register indentation level
    // TODO: Why is this not picked up by the main loop?
    if (indentation_records && current_line_indentation != prev_line_indentation) {
        indentation_records->push_back(
            IndentRecord { current_line, current_line_indentation, current_scope, prev_line_token_index});
    }

    return result;
}