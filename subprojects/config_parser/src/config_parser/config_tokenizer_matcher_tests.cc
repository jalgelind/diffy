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
                text += tokens[i].str_from(token_text);;
                drop_newlines = false;
                drop_spaces = false;
            }
        }
        return text;
    }

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

    std::vector<SequencePoint> while_loop_sequence {
        SequencePoint { TokenId_Identifier, "while" },
        SequencePoint { TokenId_OpenParen },
        SequencePoint { TokenId_Any },
        SequencePoint { TokenId_CloseParen },
        SequencePoint { TokenId_OpenCurly },
    };

    std::vector<SequencePoint> if_cond_sequence {
        SequencePoint { TokenId_Identifier, "if" },
        SequencePoint { TokenId_OpenParen },
        SequencePoint { TokenId_Any },
        SequencePoint { TokenId_CloseParen },
        SequencePoint { TokenId_OpenCurly },
    };

    std::vector<SequencePoint> switch_cond_sequence {
        SequencePoint { TokenId_Identifier, "switch" },
        SequencePoint { TokenId_OpenParen },
        SequencePoint { TokenId_Any },
        SequencePoint { TokenId_CloseParen },
        SequencePoint { TokenId_OpenCurly },
    };

    std::vector<SequencePoint> typedef_sequence {
        SequencePoint { TokenId_Identifier, "typedef" },
        SequencePoint { TokenId_Identifier }, // enum, struct etc
        SequencePoint { TokenId_Identifier }, // name
        SequencePoint { TokenId_OpenCurly },
    };

    std::vector<SequencePoint> typedef2_sequence {
        SequencePoint { TokenId_Identifier, "typedef" },
        SequencePoint { TokenId_Identifier }, // enum, struct etc
        SequencePoint { TokenId_Identifier }, // name
        SequencePoint { TokenId_OpenCurly },
    };

    // fn (...) {
    std::vector<SequencePoint> fn_sequence {
        SequencePoint { TokenId_Identifier, "fn" },
        SequencePoint { TokenId_OpenParen },
        SequencePoint { TokenId_Any },
        SequencePoint { TokenId_CloseParen },
        SequencePoint { TokenId_OpenCurly },
    };

    // def ...:
    std::vector<SequencePoint> def_sequence {
        SequencePoint { TokenId_Identifier, "def" },
        SequencePoint { TokenId_Any },
        SequencePoint { TokenId_Identifier, ":" }, // TODO: hm! TokenId_Colon
    };

    // Good enough.
    std::vector<SequencePoint> function_sequence {
        SequencePoint { TokenId_Identifier },
        SequencePoint { TokenId_OpenParen },
        SequencePoint { TokenId_Any },
        SequencePoint { TokenId_CloseParen },
        SequencePoint { TokenId_OpenCurly },
    };

    // ?
    std::vector<SequencePoint> other_sequence {
        SequencePoint { TokenId_Semicolon },
        SequencePoint { TokenId_OpenCurly },
    };

    struct NamedSequencePoint {
        std::string name;
        std::vector<SequencePoint>& sp;
    };
    std::vector<NamedSequencePoint> test_pattern_sequences {
        {"loop/for"        , for_loop_sequence},
        {"typedef/1"       , typedef_sequence},
        {"typedef/2"       , typedef2_sequence},
        {"loop/while"      , while_loop_sequence},
        {"cond/if"         , if_cond_sequence},
        {"cond/switch"     , switch_cond_sequence},
        {"func/1"          , fn_sequence},
        {"func/2"          , def_sequence},
        {"func/0"          , function_sequence},
        {"misc/semicurl"   , other_sequence}
    };
}

SequenceMatch match_sequence(std::vector<Token>& input_tokens, const std::string& input_text, std::string* out_source) {
    SequenceMatch match;
    for (auto& pattern : test_pattern_sequences) {
        if (reverse_find_sequence(input_tokens, input_text, pattern.sp, &match)) {
            // We should also get the indentation level and scope level of the match
            fmt::print("Found {} at pos: {}..{}\n", pattern.name, match.start, match.end);
            if (out_source) *out_source = pattern.name;
            break;
        }
    }
    return match;
}

std::vector<Token> tokenize(const std::string& text) {
    config_tokenizer::ParseOptions options;
    options.strip_spaces = false;
    options.strip_newlines = false;
    options.strip_annotated_string_tokens = false;
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
            for (int i = 0 ; i < 3; i++)  {
                i++;
            }
        )foo";

        auto tokens = tokenize(text);

        std::string source;
        SequenceMatch match = match_sequence(tokens, text, &source);
        REQUIRE(source == "loop/for");
;
        auto rendered = render_sequence(tokens, text, match);

        REQUIRE(rendered == "for (int i = 0 ; i < 3; i++) {");
    }

    SUBCASE("match-while-loop") {
        std::string text = R"foo({
            int main() {
            while (true && apa == 'bepa') {
                i++;
            }
        )foo";

        auto tokens = tokenize(text);

        std::string source;
        SequenceMatch match = match_sequence(tokens, text, &source);
        REQUIRE(source == "loop/while");

        std::vector<SequencePoint> for_loop_sequence {
            SequencePoint { TokenId_Identifier, "while" },
            SequencePoint { TokenId_OpenParen },
            SequencePoint { TokenId_Any },
            SequencePoint { TokenId_CloseParen },
            SequencePoint { TokenId_OpenCurly },
        };

        //config_tokenizer::token_dump(tokens, text);
        auto rendered = render_sequence(tokens, text, match);
        REQUIRE(rendered == "while (true && apa == 'bepa') {");

    }
}