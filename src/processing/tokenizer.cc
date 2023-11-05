#include "tokenizer.hpp"

#include "util/hash.hpp"
#include "util/readlines.hpp"

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

}  // namespace

#if 0
std::string
diffy::repr(Token& token) {
    return fmt::format("Token(.start={:>3}, .length={:>3}, .flags={}, .hash={:8X})",
        token.start, token.length, token.flags, token.hash);
}

std::string
diffy::repr(Token& token, const std::string& source_text) {
    auto s = diffy::escape_whitespace(token.str_from(source_text));
    return fmt::format("Token(.start={:>3}, .length={:>3}, .flags={}, .text='{}')",
        token.start, token.length, token.flags, s);
}
#endif

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
diffy::tokenize(const std::string& text) {
    std::vector<Token> result;

    // Nothing to do.
    if (text.size() == 0) {
        return result;
    }

    auto maxlen = text.size() - 1;
    std::size_t seeker = 0;

    do {
        auto start_idx = seeker;
        char c = text[start_idx];
        TokenFlag token_flags = TokenFlagNone;
        if (c == ' ') {
            while (seeker <= maxlen && text[seeker] == ' ') {
                seeker++;
            };
            token_flags = TokenFlagSpace;
        } else if (c == '\t') {
            while (seeker <= maxlen && text[seeker] == '\t') {
                seeker++;
            };
            token_flags = TokenFlagTab;
        } else if (c == '\n') {
            while (seeker <= maxlen && text[seeker] == '\n') {
                seeker++;
            };
            token_flags = TokenFlagLF;
        } else if (c == '\r') {
            while (seeker <= maxlen && text[seeker] == '\r') {
                seeker++;
            };
            token_flags = TokenFlagCR;
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

    return result;
}
