#pragma once

/*
    Tokenizer adapted to parse config files. It tries to be a little bit smart
    in tagging the in-between token data as strings/integers/booleans and identifiers.

    I guess it's a bit of a lexer? It should make parsing easier anyway.

    TODO: Handle escape sequences, https://en.wikipedia.org/wiki/INI_file#Escape_characters
*/

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {
namespace config_tokenizer {

using TokenId = std::uint32_t;
// clang-format off
const TokenId TokenId_Space        = 1 << 0;
const TokenId TokenId_Newline      = 1 << 1;
const TokenId TokenId_OpenBracket  = 1 << 2;
const TokenId TokenId_CloseBracket = 1 << 3;
const TokenId TokenId_Assign       = 1 << 4;
const TokenId TokenId_OpenCurly    = 1 << 5;
const TokenId TokenId_CloseCurly   = 1 << 6;
const TokenId TokenId_DoubleQuote  = 1 << 7;
const TokenId TokenId_SingleQuote  = 1 << 8;
const TokenId TokenId_Hashtag      = 1 << 9;
const TokenId TokenId_Comma        = 1 << 10;
const TokenId TokenId_Backslash    = 1 << 11;
const TokenId TokenId_Doubleslash  = 1 << 12;
const TokenId TokenId_Boolean      = 1 << 13;
const TokenId TokenId_Integer      = 1 << 14;
const TokenId TokenId_Float        = 1 << 15;
const TokenId TokenId_String       = 1 << 16;
const TokenId TokenId_Identifier   = 1 << 17;
const TokenId TokenId_Comment      = 1 << 18;
const TokenId TokenId_Terminator   = 1 << 19;

const TokenId TokenId_FirstOnLine  = 1 << 20; // Only whitespace before this token

const TokenId TokenId_MetaValue = TokenId_Boolean | TokenId_Integer | TokenId_String;
const TokenId TokenId_MetaObject = TokenId_OpenCurly | TokenId_OpenBracket | TokenId_MetaValue;

// clang-format on

struct Token {
    std::string::size_type start = 0;
    std::string::size_type length = 0;

    std::string::size_type line = 0;
    std::string::size_type column = 0;

    std::size_t sequence_index;

    TokenId id = 0;

    bool token_boolean_arg = false;
    int token_int_arg = 0;
    float token_float_arg = 0;

    const std::string
    str_from(const std::string& line) const {
        return line.substr(this->start, this->length);
    }

    const std::string
    str_display_from(const std::string& line) const;
};

struct ParseOptions {
    bool strip_spaces = true;
    bool strip_newlines = false;
    bool strip_quotes = false;
    bool strip_annotated_string_tokens = false;
    bool strip_comments = false;
    bool append_terminator = false;
};

struct ParseResult {
    bool ok;
    std::vector<Token> tokens;
    std::string error;
};

bool
tokenize(const std::string& text, ParseOptions& options, ParseResult& result);

void
token_dump(std::vector<Token> tokens, const std::string& source_text);

bool
is_whitespace(char c);

std::string
repr(TokenId id);

}  // namespace config_tokenizer
}  // namespace diffy