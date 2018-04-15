#include "tokenizer.hpp"

#include "util/hash.hpp"

#include <doctest.h>

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

    // TODO: Recognize leading or trailing whitespace.
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

        // HACK: Combine CR+LF into single token.
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

#ifndef DOCTEST_CONFIG_DISABLE

// HACK: Without this, we'll get link errors on Darwin.
// See: https://github.com/onqtam/doctest/issues/126
#include <iostream>

#endif

TEST_CASE("tokenizer") {
    SUBCASE("empty") {
        auto a = diffy::tokenize("");
        REQUIRE(a.size() == 0);
    }

    SUBCASE("just_newline") {
        auto line = "\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "\n");
    }

    SUBCASE("multiple_newlines") {
        auto line = "apa\nbepa\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 4);
        REQUIRE(a[0].str_from(line) == "apa");
        REQUIRE(a[1].str_from(line) == "\n");
        REQUIRE(a[2].str_from(line) == "bepa");
        REQUIRE(a[3].str_from(line) == "\n");
    }

    SUBCASE("mixed1") {
        auto line = "  apa(bepa \n||  cepa)\t{\r\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 13);
        REQUIRE(a[0].str_from(line) == "  ");
        REQUIRE(a[1].str_from(line) == "apa");
        REQUIRE(a[2].str_from(line) == "(");
        REQUIRE(a[3].str_from(line) == "bepa");
        REQUIRE(a[4].str_from(line) == " ");
        REQUIRE(a[5].str_from(line) == "\n");
        REQUIRE(a[6].str_from(line) == "||");
        REQUIRE(a[7].str_from(line) == "  ");
        REQUIRE(a[8].str_from(line) == "cepa");
        REQUIRE(a[9].str_from(line) == ")");
        REQUIRE(a[10].str_from(line) == "\t");
        REQUIRE(a[11].str_from(line) == "{");
        REQUIRE(a[12].str_from(line) == "\r\n");
    }

    SUBCASE("delimiters") {
        auto line = "a->b.c\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 7);
        REQUIRE(a[0].str_from(line) == "a");
        REQUIRE(a[1].str_from(line) == "-");
        REQUIRE(a[2].str_from(line) == ">");
        REQUIRE(a[3].str_from(line) == "b");
        REQUIRE(a[4].str_from(line) == ".");
        REQUIRE(a[5].str_from(line) == "c");
        REQUIRE(a[6].str_from(line) == "\n");
    }

    SUBCASE("utf8") {
        auto line = "ö->å.ä\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 7);
        REQUIRE(a[0].str_from(line) == "ö");
        REQUIRE(a[1].str_from(line) == "-");
        REQUIRE(a[2].str_from(line) == ">");
        REQUIRE(a[3].str_from(line) == "å");
        REQUIRE(a[4].str_from(line) == ".");
        REQUIRE(a[5].str_from(line) == "ä");
        REQUIRE(a[6].str_from(line) == "\n");
    }
}