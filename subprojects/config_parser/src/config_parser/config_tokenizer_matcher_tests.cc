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

TEST_CASE("config_tokenizer_matcher") {
    SUBCASE("match-for-loop") {
        std::string text = R"foo({
int main() {
  for (int i = 0; i < 3; i++) {
      i++;
})foo";

        config_tokenizer::ParseOptions options;
        options.strip_spaces = false;
        options.strip_newlines = false;
        options.strip_annotated_string_tokens = true;
        options.strip_comments = true;
        
        std::vector<config_tokenizer::Token> tokens;
        config_tokenizer::ParseResult parse_results;
        REQUIRE(config_tokenizer::tokenize(text, options, parse_results));
        tokens = parse_results.tokens;


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
        // REQUIRE(match_sequence == "for (int i = 0; i < 3; i++) {");
        REQUIRE(match.start == 12);
        REQUIRE(match.end == 35);

    }
}