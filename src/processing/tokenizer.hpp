#pragma once

/*
    Scan through a text string and split it up into a vector of Tokens.

    A token is a subsequence of the string consisting of similar or related
    characters. Visually distinct chunks of text; numbers, strings,
    whitespace, delimiters.
*/

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

using TokenFlag = std::uint8_t;
const TokenFlag TokenFlagNone = 0;
const TokenFlag TokenFlagSpace = 1 << 0;
const TokenFlag TokenFlagTab = 1 << 1;
const TokenFlag TokenFlagCR = 1 << 2;
const TokenFlag TokenFlagLF = 1 << 3;
const TokenFlag TokenFlagCRLF = 1 << 4;

struct Token {
    std::string::size_type start = 0;
    std::string::size_type length = 0;
    std::uint32_t hash;
    TokenFlag flags;

    const std::string
    str_from(const std::string line) const {
        return line.substr(this->start, this->length);
    }
};

std::string
repr(Token& token);

std::string
repr(Token& token, const std::string& source_text);

struct IndentRecord {
    int line;
    int indentation;
    int scope;
    std::size_t token_offset;
};

bool
is_whitespace(char c);

bool
is_empty(const std::string& s);

std::vector<Token>
tokenize(const std::string& text, std::vector<IndentRecord>* indentation_records = nullptr);

}  // namespace diffy