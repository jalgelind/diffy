#include "config_tokenizer.hpp"

#include <doctest.h>

#include <fmt/format.h>

#include <iostream>
#include <string>
#include <vector>

using namespace diffy;
using namespace diffy::config_tokenizer;

TEST_CASE("tokenizer") {
    SUBCASE("empty") {
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize("", options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 0);
    }

    SUBCASE("just_newline") {
        auto line = "\n";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "\n");
    }

    SUBCASE("strip spaces") {
        auto line = "   \n   ";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        options.strip_spaces = true;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "\n");
    }

    SUBCASE("unterminated string") {
        auto line = "   '   ";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize(line, options, result);
        REQUIRE(result.ok == false);
    }

    SUBCASE("tokens") {
        auto line = "{}[]= \n";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        options.strip_spaces = false;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 7);
        REQUIRE(a[0].str_from(line) == "{");
        REQUIRE(a[1].str_from(line) == "}");
        REQUIRE(a[2].str_from(line) == "[");
        REQUIRE(a[3].str_from(line) == "]");
        REQUIRE(a[4].str_from(line) == "=");
        REQUIRE(a[5].str_from(line) == " ");
        REQUIRE(a[6].str_from(line) == "\n");
    }

    SUBCASE("section") {
        auto line = "[test]";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 3);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE(a[1].str_from(line) == "test");
        REQUIRE(a[2].str_from(line) == "]");
    }

    SUBCASE("section quoted name") {
        auto line = "[\"space test\"]";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 5);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE(a[1].str_from(line) == "\"");
        REQUIRE(a[2].str_from(line) == "space test");
        REQUIRE(a[3].str_from(line) == "\"");
        REQUIRE(a[4].str_from(line) == "]");
    }

    SUBCASE("section with key") {
        auto line = "[test]\n key=\"value str\"";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        options.strip_spaces = false;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 10);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE((a[0].id & (TokenId_OpenBracket)) == (TokenId_OpenBracket));
        REQUIRE(a[1].str_from(line) == "test");
        REQUIRE((a[1].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[2].str_from(line) == "]");
        REQUIRE((a[2].id & (TokenId_CloseBracket)) == (TokenId_CloseBracket));
        REQUIRE(a[3].str_from(line) == "\n");
        REQUIRE((a[3].id & (TokenId_Newline)) == (TokenId_Newline));
        REQUIRE(a[4].str_from(line) == " ");
        REQUIRE((a[4].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[5].str_from(line) == "key");
        REQUIRE((a[5].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[6].str_from(line) == "=");
        REQUIRE((a[6].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[7].str_from(line) == "\"");
        REQUIRE((a[7].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
        REQUIRE(a[8].str_from(line) == "value str");
        REQUIRE((a[8].id & (TokenId_String)) == (TokenId_String));
        REQUIRE(a[9].str_from(line) == "\"");
        REQUIRE((a[9].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
    }

    SUBCASE("section with integer key") {
        auto line = "[test]\n key=123";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        options.strip_spaces = false;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        // diffy::config_tokenizer::token_dump(a, line, true);
        REQUIRE(a.size() == 8);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE(a[1].str_from(line) == "test");
        REQUIRE(a[2].str_from(line) == "]");
        REQUIRE(a[3].str_from(line) == "\n");
        REQUIRE(a[4].str_from(line) == " ");
        REQUIRE(a[5].str_from(line) == "key");
        REQUIRE(a[6].str_from(line) == "=");
        REQUIRE(a[7].str_from(line) == "123");

        REQUIRE((a[0].id & TokenId_OpenBracket) == TokenId_OpenBracket);
        REQUIRE((a[1].id & TokenId_Identifier) == TokenId_Identifier);
        REQUIRE((a[2].id & TokenId_CloseBracket) == TokenId_CloseBracket);
        REQUIRE((a[3].id & TokenId_Newline) == TokenId_Newline);
        REQUIRE((a[4].id & TokenId_Space) == TokenId_Space);
        REQUIRE((a[5].id & TokenId_Identifier) == TokenId_Identifier);
        REQUIRE((a[6].id & TokenId_Assign) == TokenId_Assign);
        REQUIRE((a[7].id & TokenId_Integer) == TokenId_Integer);
    }

    SUBCASE("section with table key") {
        auto line = "[test]\n key = { apa = 123, bepa = 456 }";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        options.strip_spaces = false;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;

        // diffy::config_tokenizer::token_dump(a, line, true);

        REQUIRE(a.size() == 25);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE((a[0].id & (TokenId_OpenBracket)) == (TokenId_OpenBracket));
        REQUIRE(a[1].str_from(line) == "test");
        REQUIRE((a[1].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[2].str_from(line) == "]");
        REQUIRE((a[2].id & (TokenId_CloseBracket)) == (TokenId_CloseBracket));
        REQUIRE(a[3].str_from(line) == "\n");
        REQUIRE((a[3].id & (TokenId_Newline)) == (TokenId_Newline));
        REQUIRE(a[4].str_from(line) == " ");
        REQUIRE((a[4].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[5].str_from(line) == "key");
        REQUIRE((a[5].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[6].str_from(line) == " ");
        REQUIRE((a[6].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[7].str_from(line) == "=");
        REQUIRE((a[7].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[8].str_from(line) == " ");
        REQUIRE((a[8].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[9].str_from(line) == "{");
        REQUIRE((a[9].id & (TokenId_OpenCurly)) == (TokenId_OpenCurly));
        REQUIRE(a[10].str_from(line) == " ");
        REQUIRE((a[10].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[11].str_from(line) == "apa");
        REQUIRE((a[11].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[12].str_from(line) == " ");
        REQUIRE((a[12].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[13].str_from(line) == "=");
        REQUIRE((a[13].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[14].str_from(line) == " ");
        REQUIRE((a[14].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[15].str_from(line) == "123");
        REQUIRE((a[15].id & (TokenId_Integer)) == (TokenId_Integer));
        REQUIRE(a[16].str_from(line) == ",");
        REQUIRE((a[16].id & (TokenId_Comma)) == (TokenId_Comma));
        REQUIRE(a[17].str_from(line) == " ");
        REQUIRE((a[17].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[18].str_from(line) == "bepa");
        REQUIRE((a[18].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[19].str_from(line) == " ");
        REQUIRE((a[19].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[20].str_from(line) == "=");
        REQUIRE((a[20].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[21].str_from(line) == " ");
        REQUIRE((a[21].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[22].str_from(line) == "456");
        REQUIRE((a[22].id & (TokenId_Integer)) == (TokenId_Integer));
        REQUIRE(a[23].str_from(line) == " ");
        REQUIRE((a[23].id & (TokenId_Space)) == (TokenId_Space));
        REQUIRE(a[24].str_from(line) == "}");
        REQUIRE((a[24].id & (TokenId_CloseCurly)) == (TokenId_CloseCurly));
    }

    SUBCASE("sections") {
        auto line = "[test]  \"\"  \n [other_section] ";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;

        // diffy::config_tokenizer::token_dump(a, line, true);

        REQUIRE(a.size() == 10);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE((a[0].id & (TokenId_OpenBracket)) == (TokenId_OpenBracket));
        REQUIRE(a[1].str_from(line) == "test");
        REQUIRE((a[1].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[2].str_from(line) == "]");
        REQUIRE((a[2].id & (TokenId_CloseBracket)) == (TokenId_CloseBracket));
        REQUIRE(a[3].str_from(line) == "\"");
        REQUIRE((a[3].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
        REQUIRE(a[4].str_from(line) == "");
        REQUIRE((a[4].id & (TokenId_String)) == (TokenId_String));
        REQUIRE(a[5].str_from(line) == "\"");
        REQUIRE((a[5].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
        REQUIRE(a[6].str_from(line) == "\n");
        REQUIRE((a[6].id & (TokenId_Newline)) == (TokenId_Newline));
        REQUIRE(a[7].str_from(line) == "[");
        REQUIRE((a[7].id & (TokenId_OpenBracket)) == (TokenId_OpenBracket));
        REQUIRE(a[8].str_from(line) == "other_section");
        REQUIRE((a[8].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[9].str_from(line) == "]");
        REQUIRE((a[9].id & (TokenId_CloseBracket)) == (TokenId_CloseBracket));
    }

    SUBCASE("sections_with_keys") {
        auto line = "[test]\nkey=\"value\"    \n[other_section] key=1234 otherk=\"value\"";
        diffy::config_tokenizer::ParseResult result;
        diffy::config_tokenizer::ParseOptions options;
        diffy::config_tokenizer::tokenize(line, options, result);
        auto a = result.tokens;
        // diffy::config_tokenizer::token_dump(a, line, true);

        REQUIRE(a.size() == 21);
        REQUIRE(a[0].str_from(line) == "[");
        REQUIRE((a[0].id & (TokenId_OpenBracket)) == (TokenId_OpenBracket));
        REQUIRE(a[1].str_from(line) == "test");
        REQUIRE((a[1].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[2].str_from(line) == "]");
        REQUIRE((a[2].id & (TokenId_CloseBracket)) == (TokenId_CloseBracket));
        REQUIRE(a[3].str_from(line) == "\n");
        REQUIRE((a[3].id & (TokenId_Newline)) == (TokenId_Newline));
        REQUIRE(a[4].str_from(line) == "key");
        REQUIRE((a[4].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[5].str_from(line) == "=");
        REQUIRE((a[5].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[6].str_from(line) == "\"");
        REQUIRE((a[6].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
        REQUIRE(a[7].str_from(line) == "value");
        REQUIRE((a[7].id & (TokenId_String)) == (TokenId_String));
        REQUIRE(a[8].str_from(line) == "\"");
        REQUIRE((a[8].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
        REQUIRE(a[9].str_from(line) == "\n");
        REQUIRE((a[9].id & (TokenId_Newline)) == (TokenId_Newline));
        REQUIRE(a[10].str_from(line) == "[");
        REQUIRE((a[10].id & (TokenId_OpenBracket)) == (TokenId_OpenBracket));
        REQUIRE(a[11].str_from(line) == "other_section");
        REQUIRE((a[11].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[12].str_from(line) == "]");
        REQUIRE((a[12].id & (TokenId_CloseBracket)) == (TokenId_CloseBracket));
        REQUIRE(a[13].str_from(line) == "key");
        REQUIRE((a[13].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[14].str_from(line) == "=");
        REQUIRE((a[14].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[15].str_from(line) == "1234");
        REQUIRE((a[15].id & (TokenId_Integer)) == (TokenId_Integer));
        REQUIRE(a[16].str_from(line) == "otherk");
        REQUIRE((a[16].id & (TokenId_Identifier)) == (TokenId_Identifier));
        REQUIRE(a[17].str_from(line) == "=");
        REQUIRE((a[17].id & (TokenId_Assign)) == (TokenId_Assign));
        REQUIRE(a[18].str_from(line) == "\"");
        REQUIRE((a[18].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
        REQUIRE(a[19].str_from(line) == "value");
        REQUIRE((a[19].id & (TokenId_String)) == (TokenId_String));
        REQUIRE(a[20].str_from(line) == "\"");
        REQUIRE((a[20].id & (TokenId_DoubleQuote)) == (TokenId_DoubleQuote));
    }

    SUBCASE("plain integer") {
        auto line = "12345";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "12345");
        REQUIRE((a[0].id & TokenId_Integer) == TokenId_Integer);
        REQUIRE(a[0].token_int_arg == 12345);
    }

    SUBCASE("float literal is tagged Float (regression D1)") {
        auto line = "3.5";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "3.5");
        REQUIRE((a[0].id & TokenId_Float) == TokenId_Float);
        REQUIRE(a[0].token_float_arg == doctest::Approx(3.5f));
    }

    SUBCASE("digits followed by letters are an identifier, not an integer (regression D2)") {
        auto line = "12abc";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        auto a = result.tokens;
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "12abc");
        REQUIRE((a[0].id & TokenId_Integer) != TokenId_Integer);
        REQUIRE((a[0].id & TokenId_Identifier) == TokenId_Identifier);
    }
}

TEST_CASE("escaped strings") {
    SUBCASE("double-quoted string captures an escaped quote (doesn't terminate early)") {
        // Text: "a\"b"  (an escaped double-quote in the middle)
        auto line = "\"a\\\"b\"";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok);
        auto a = result.tokens;
        REQUIRE(a.size() == 3);  // DoubleQuote, String, DoubleQuote
        REQUIRE((a[0].id & TokenId_DoubleQuote) == TokenId_DoubleQuote);
        REQUIRE((a[1].id & TokenId_String) == TokenId_String);
        // Double-quoted strings are tagged escaped so the parser unescapes them.
        REQUIRE((a[1].id & TokenId_EscapedString) == TokenId_EscapedString);
        REQUIRE(a[1].str_from(line) == "a\\\"b");  // raw text still carries the backslash
        REQUIRE((a[2].id & TokenId_DoubleQuote) == TokenId_DoubleQuote);
    }

    SUBCASE("single-quoted string is a raw literal (not tagged escaped)") {
        // Text: 'a\b'  (a backslash that must be preserved verbatim, e.g. a path)
        auto line = "'a\\b'";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok);
        auto a = result.tokens;
        REQUIRE(a.size() == 3);
        REQUIRE((a[1].id & TokenId_String) == TokenId_String);
        REQUIRE((a[1].id & TokenId_EscapedString) != TokenId_EscapedString);
        REQUIRE(a[1].str_from(line) == "a\\b");
    }

    SUBCASE("escaped backslash then closing quote terminates") {
        // Text: "a\\"  -> content is a backslash; the \\ pair doesn't swallow the quote
        auto line = "\"a\\\\\"";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok);
        auto a = result.tokens;
        REQUIRE(a.size() == 3);
        REQUIRE(a[1].str_from(line) == "a\\\\");
    }

    SUBCASE("dangling backslash at end of a double-quoted string is unterminated") {
        auto line = "\"abc\\";  // "abc\   (backslash then EOF)
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok == false);
    }

    SUBCASE("newline inside a double-quoted string is an error") {
        auto line = "\"abc\ndef\"";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok == false);
    }
}

TEST_CASE("adjacent quoted literals (multi-line strings)") {
    // Multi-line string values are written as one quoted literal per line; the
    // tokenizer emits each as its own String token and flags the continuation as
    // first-on-line, which the parser uses to concatenate them with '\n'.
    SUBCASE("two literals on separate lines are two string tokens") {
        auto line = "'first'\n'second'";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok);
        std::vector<Token> strings;
        for (auto& t : result.tokens) {
            if (t.id & TokenId_String) {
                strings.push_back(t);
            }
        }
        REQUIRE(strings.size() == 2);
        REQUIRE(strings[0].str_from(line) == "first");
        REQUIRE(strings[1].str_from(line) == "second");
        // The continuation begins its own line.
        REQUIRE((strings[1].id & TokenId_FirstOnLine) == TokenId_FirstOnLine);
    }

    SUBCASE("a newline inside a single-quoted string is an error") {
        auto line = "'abc\ndef'";
        ParseResult result;
        ParseOptions options;
        tokenize(line, options, result);
        REQUIRE(result.ok == false);
    }
}

TEST_CASE("escape helpers") {
    SUBCASE("escape_string escapes the specials") {
        REQUIRE(escape_string("a\tb\"c\\d\ne") == "a\\tb\\\"c\\\\d\\ne");
    }
    SUBCASE("unescape_string reverses escape_string") {
        const std::string s = "tab\tquote\"back\\slash\nline\rcarriage";
        REQUIRE(unescape_string(escape_string(s)) == s);
    }
    SUBCASE("unescape_string preserves unknown escapes") {
        REQUIRE(unescape_string("a\\qb") == "a\\qb");
    }
    SUBCASE("unescape_string tolerates a trailing backslash") {
        REQUIRE(unescape_string("abc\\") == "abc\\");
    }
}
