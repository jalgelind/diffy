#include "config_parser.hpp"
#include "config_parser_utils.hpp"
#include "config_serializer.hpp"
#include "config_tokenizer_matcher.hpp"

#include <doctest.h>

#include <fmt/format.h>

#include <string>
#include <vector>

using namespace diffy;
using namespace config_tokenizer;

namespace {
    // Duplicated from diffy suggestion engine
    std::string
    render_sequence(const std::vector<Token> tokens, const std::string& token_text,
            SequenceMatch match) {
        bool drop_newlines = true;
        bool drop_spaces = true;
        bool drop_curlies = true;
        std::string text;
        for (int i = match.start; i < match.end; i++) {
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

std::vector<Token> tokenize(const std::string& text) {
    config_tokenizer::ParseOptions options;
    options.strip_spaces = false;
    options.strip_newlines = false;
    options.strip_annotated_string_tokens = true;
    options.strip_comments = true;
    
    std::vector<config_tokenizer::Token> tokens;
    config_tokenizer::ParseResult parse_results;
    REQUIRE(config_tokenizer::tokenize(text, options, parse_results));
    tokens = parse_results.tokens;
    return tokens;
}

TEST_CASE("config_tokenizer_matcher") {
    SUBCASE("match-for-loop") {
        std::string text = R"foo({
int main() {
  for (int i = 0; i < 3; i++) {
      i++;
})foo";

        auto tokens = tokenize(text);

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

        SequenceMatch match;
        reverse_find_sequence(tokens, text, for_loop_sequence, &match);
        auto rendered = render_sequence(tokens, text, match);

        REQUIRE(rendered == "for (int i = 0; i < 3; i++) {");
        REQUIRE(match.start == 12);
        REQUIRE(match.end == 35);
    }

    SUBCASE("match-while-loop") {
        std::string text = R"foo({
int main() {
  while (true && apa == 'bepa') {
      i++;
})foo";

        auto tokens = tokenize(text);

        std::vector<SequencePoint> for_loop_sequence {
            SequencePoint { TokenId_Identifier, "while" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        SequenceMatch match;
        reverse_find_sequence(tokens, text, for_loop_sequence, &match);
        auto rendered = render_sequence(tokens, text, match);
        fmt::print("'{}'\n", rendered);
        REQUIRE(rendered == "while (true && apa == 'bepa') {");

    }
}