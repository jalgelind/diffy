#include "config_parser.hpp"
#include "config_parser_utils.hpp"

#include <doctest.h>

#include <fmt/format.h>

#include <string>
#include <vector>

using namespace diffy;

void
dump_instructions(std::vector<TbInstruction> instructions) {
    int index = 0;
    fmt::print("// clang-format off\n");
    fmt::print("REQUIRE(instructions.size() == {});\n", instructions.size());
    int depth = 0;
    for (auto ins : instructions) {
        if (ins.op == TbOperator::TableEnd || ins.op == TbOperator::ArrayEnd)
            depth--;

        std::string ctor_name;
        std::string ctor_args;
        switch (ins.op) {
            case TbOperator::Comment: {
                ctor_name = "Comment";
                ctor_args = "\"" + ins.oparg_string + "\"";
            } break;
            case TbOperator::Key: {
                ctor_name = "Key";
                ctor_args = "\"" + ins.oparg_string + "\"";
            } break;
            case TbOperator::TableStart: {
                ctor_name = "TableStart";
                ctor_args = "\"" + ins.oparg_string + "\"";
            } break;
            case TbOperator::TableEnd: {
                ctor_name = "TableEnd";
                ctor_args = "\"" + ins.oparg_string + "\"";
            } break;
            case TbOperator::ArrayStart: {
                ctor_name = "ArrayStart";
                ctor_args = "\"" + ins.oparg_string + "\"";
            } break;
            case TbOperator::ArrayEnd: {
                ctor_name = "ArrayEnd";
                ctor_args = "\"" + ins.oparg_string + "\"";
            } break;
            case TbOperator::Value: {
                switch (ins.oparg_type) {
                    case TbValueType::None: {
                        ctor_name = "Value";
                        ctor_args = "we don't have None values?";
                    } break;
                    case TbValueType::Int: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("{}", ins.oparg_int);
                    } break;
                    case TbValueType::Bool: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("{}", ins.oparg_bool);
                    } break;
                    case TbValueType::String: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("\"{}\"", ins.oparg_string);
                    } break;
                    case TbValueType::Float: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("{}", ins.oparg_float);
                    } break;
                }
            } break;
        }

        // Skip empty strings on non-values
        if (ins.op != TbOperator::Value && ctor_args == "\"\"") {
            ctor_args = "";
        }

        auto sdepth = std::string(depth < 0 ? 0 : depth * 2, ' ');
        fmt::print("REQUIRE(instructions[{:>3}] == {}TbInstruction::{}({}));\n", index, sdepth, ctor_name,
                   ctor_args);

        if (ins.op == TbOperator::TableStart || ins.op == TbOperator::ArrayStart)
            depth++;

        index++;
    }
    fmt::print("// clang-format on\n");
}

TEST_CASE("parser") {
    SUBCASE("naked table value") {
        std::string cfg_text = "fg='white', bg='default', attr=['underline']";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }

        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 10);
        REQUIRE(instructions[  0] == TbInstruction::TableStart());
        REQUIRE(instructions[  1] ==   TbInstruction::Key("fg"));
        REQUIRE(instructions[  2] ==   TbInstruction::Value("white"));
        REQUIRE(instructions[  3] ==   TbInstruction::Key("bg"));
        REQUIRE(instructions[  4] ==   TbInstruction::Value("default"));
        REQUIRE(instructions[  5] ==   TbInstruction::Key("attr"));
        REQUIRE(instructions[  6] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[  7] ==     TbInstruction::Value("underline"));
        REQUIRE(instructions[  8] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[  9] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("table value") {
        std::string cfg_text = "{fg='white', bg='default', attr=['underline']}";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 10);
        REQUIRE(instructions[  0] == TbInstruction::TableStart());
        REQUIRE(instructions[  1] ==   TbInstruction::Key("fg"));
        REQUIRE(instructions[  2] ==   TbInstruction::Value("white"));
        REQUIRE(instructions[  3] ==   TbInstruction::Key("bg"));
        REQUIRE(instructions[  4] ==   TbInstruction::Value("default"));
        REQUIRE(instructions[  5] ==   TbInstruction::Key("attr"));
        REQUIRE(instructions[  6] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[  7] ==     TbInstruction::Value("underline"));
        REQUIRE(instructions[  8] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[  9] == TbInstruction::TableEnd());
        // clang-format on
    }

    //  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    SUBCASE("bare section") {
        std::string cfg_text = "[section]";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);

        // clang-format off
        REQUIRE(instructions.size() == 3);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart());
        REQUIRE(instructions[  2] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("bare section with spaces") {
        std::string cfg_text = R"foo(
            [section]
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 3);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart());
        REQUIRE(instructions[  2] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("bare section with leading comments") {
        std::string cfg_text = R"foo(
            # first comment
            // second comment
            [section]
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 5);
        REQUIRE(instructions[  0] == TbInstruction::Comment("# first comment"));
        REQUIRE(instructions[  1] == TbInstruction::Comment("// second comment"));
        REQUIRE(instructions[  2] == TbInstruction::Key("section"));
        REQUIRE(instructions[  3] == TbInstruction::TableStart());
        REQUIRE(instructions[  4] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("single section, single key") {
        std::string cfg_text = R"foo(
            [section]
                my_key = "hey"
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 5);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart());
        REQUIRE(instructions[  2] ==   TbInstruction::Key("my_key"));
        REQUIRE(instructions[  3] ==   TbInstruction::Value("hey"));
        REQUIRE(instructions[  4] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("single section, multiple keys") {
        std::string cfg_text = R"foo(
            [section]
                my_key = "hey"
                my_other_key = 1234
                something = false
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }

        // cfg_dump_instructions(instructions)

        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 9);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart());
        REQUIRE(instructions[  2] ==   TbInstruction::Key("my_key"));
        REQUIRE(instructions[  3] ==   TbInstruction::Value("hey"));
        REQUIRE(instructions[  4] ==   TbInstruction::Key("my_other_key"));
        REQUIRE(instructions[  5] ==   TbInstruction::Value(1234));
        REQUIRE(instructions[  6] ==   TbInstruction::Key("something"));
        REQUIRE(instructions[  7] ==   TbInstruction::Value(false));
        REQUIRE(instructions[  8] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("multiple sections, multiple keys") {
        std::string cfg_text = R"foo(
            [section]
                my_key = "hey"
                my_other_key = 1234
                something = false
            [shapes]
                apa = "apa"
                bepa = 0
                cepa = off
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);

        // clang-format off
        REQUIRE(instructions.size() == 18);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart());
        REQUIRE(instructions[  2] ==   TbInstruction::Key("my_key"));
        REQUIRE(instructions[  3] ==   TbInstruction::Value("hey"));
        REQUIRE(instructions[  4] ==   TbInstruction::Key("my_other_key"));
        REQUIRE(instructions[  5] ==   TbInstruction::Value(1234));
        REQUIRE(instructions[  6] ==   TbInstruction::Key("something"));
        REQUIRE(instructions[  7] ==   TbInstruction::Value(false));
        REQUIRE(instructions[  8] == TbInstruction::TableEnd());
        REQUIRE(instructions[  9] == TbInstruction::Key("shapes"));
        REQUIRE(instructions[ 10] == TbInstruction::TableStart());
        REQUIRE(instructions[ 11] ==   TbInstruction::Key("apa"));
        REQUIRE(instructions[ 12] ==   TbInstruction::Value("apa"));
        REQUIRE(instructions[ 13] ==   TbInstruction::Key("bepa"));
        REQUIRE(instructions[ 14] ==   TbInstruction::Value(0));
        REQUIRE(instructions[ 15] ==   TbInstruction::Key("cepa"));
        REQUIRE(instructions[ 16] ==   TbInstruction::Value(false));
        REQUIRE(instructions[ 17] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("arrays") {
        std::string cfg_text = R"foo(
            [section]
                my_key = [1, 2, 3]
                my_other_key = ["one", "two", "three"]
                something = [true, on, off]
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 21);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart());
        REQUIRE(instructions[  2] ==   TbInstruction::Key("my_key"));
        REQUIRE(instructions[  3] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[  4] ==     TbInstruction::Value(1));
        REQUIRE(instructions[  5] ==     TbInstruction::Value(2));
        REQUIRE(instructions[  6] ==     TbInstruction::Value(3));
        REQUIRE(instructions[  7] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[  8] ==   TbInstruction::Key("my_other_key"));
        REQUIRE(instructions[  9] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[ 10] ==     TbInstruction::Value("one"));
        REQUIRE(instructions[ 11] ==     TbInstruction::Value("two"));
        REQUIRE(instructions[ 12] ==     TbInstruction::Value("three"));
        REQUIRE(instructions[ 13] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 14] ==   TbInstruction::Key("something"));
        REQUIRE(instructions[ 15] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[ 16] ==     TbInstruction::Value(true));
        REQUIRE(instructions[ 17] ==     TbInstruction::Value(true));
        REQUIRE(instructions[ 18] ==     TbInstruction::Value(false));
        REQUIRE(instructions[ 19] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 20] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("tables") {
        std::string cfg_text = R"foo(
            // comment
            [section]
                // comment
                my_key = { a = "a" }
                // another comment
                my_other_key = { a = 1, b = false, c = 'test', d = [1] }
                // final comment
                // it wasn't
            [other]
                // foreground
                fg = { color = { r = 0,
                                 g = 127, 
                                 b = 232
                                }
                     }
                // background
                bg = { color = { r = 0, g = 127, b = 232 } }
                
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);

        // clang-format off
        REQUIRE(instructions.size() == 27);
        REQUIRE(instructions[  0] == TbInstruction::Comment("// comment"));
        REQUIRE(instructions[  1] == TbInstruction::Key("section"));
        REQUIRE(instructions[  2] == TbInstruction::TableStart());
        REQUIRE(instructions[  3] ==   TbInstruction::Comment("// comment"));
        REQUIRE(instructions[  4] ==   TbInstruction::Key("my_key"));
        REQUIRE(instructions[  5] ==   TbInstruction::TableStart());
        REQUIRE(instructions[  6] ==     TbInstruction::Key("a"));
        REQUIRE(instructions[  7] ==     TbInstruction::Value("a"));
        REQUIRE(instructions[  8] ==     TbInstruction::Comment("// another comment"));
        REQUIRE(instructions[  9] ==   TbInstruction::TableEnd());
        REQUIRE(instructions[ 10] ==   TbInstruction::Key("my_other_key"));
        REQUIRE(instructions[ 11] ==   TbInstruction::TableStart());
        REQUIRE(instructions[ 12] ==     TbInstruction::Key("a"));
        REQUIRE(instructions[ 13] ==     TbInstruction::Value(1));
        REQUIRE(instructions[ 14] ==     TbInstruction::Key("b"));
        REQUIRE(instructions[ 15] ==     TbInstruction::Value(false));
        REQUIRE(instructions[ 16] ==     TbInstruction::Key("c"));
        REQUIRE(instructions[ 17] ==     TbInstruction::Value("test"));
        REQUIRE(instructions[ 18] ==     TbInstruction::Key("d"));
        REQUIRE(instructions[ 19] ==     TbInstruction::ArrayStart());
        REQUIRE(instructions[ 20] ==       TbInstruction::Value(1));
        REQUIRE(instructions[ 21] ==     TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 22] ==     TbInstruction::Comment("// final comment"));
        REQUIRE(instructions[ 23] ==     TbInstruction::Comment("// it wasn't"));
        REQUIRE(instructions[ 24] ==   TbInstruction::TableEnd());
        REQUIRE(instructions[ 25] == TbInstruction::TableEnd());
        REQUIRE(instructions[ 26] == TbInstruction::TableEnd());
        // clang-format on
    }

    SUBCASE("all the comments") {
        std::string cfg_text = R"foo(
            // comment 1
            // comment 2
            [section_1]
            // hello
                arr = [ // comment 3
                        {}, // comment 4
                        [ // comment 5
                            [] // comment 6
                        ] // comment 7
                ] // comment 8
                arr2 = [[], [], {}, {}] // comment 9
                dict1 = { dict2 = { arr3 = [1, 2]}} // comment 10
                apa = 6 // // comment 11
                bepa = 2 // // comment 12
            [section_2] // comment 13
                apa = 1
                // comment 14
            [section_3]
                // comment 15
                depa = {a = 1, b = 2}
                bepa = {  c = {},
                          d = [
                            1, // comment 16
                            {},
                            [],
                            [2, 3],
                            { nested = [4, 5]}
                          ],
                          e = { 
                            y = [true, false, on, off, ""],
                            f = {
                                // comment 17
                            } 
                          }
                        }
        )foo";

        std::vector<TbInstruction> instructions;
        ParseResult result;
        if (!cfg_parse_collect(cfg_text, result, instructions)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());
        // dump_instructions(instructions);

        // clang-format off
        REQUIRE(instructions.size() == 110);
        REQUIRE(instructions[  0] == TbInstruction::Comment("// comment 1"));
        REQUIRE(instructions[  1] == TbInstruction::Comment("// comment 2"));
        REQUIRE(instructions[  2] == TbInstruction::Key("section_1"));
        REQUIRE(instructions[  3] == TbInstruction::TableStart());
        REQUIRE(instructions[  4] ==   TbInstruction::Comment("// hello"));
        REQUIRE(instructions[  5] ==   TbInstruction::Key("arr"));
        REQUIRE(instructions[  6] ==   TbInstruction::Comment("// comment 3"));
        REQUIRE(instructions[  7] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[  8] ==     TbInstruction::TableStart());
        REQUIRE(instructions[  9] ==     TbInstruction::TableEnd());
        REQUIRE(instructions[ 10] ==     TbInstruction::Comment("// comment 4"));
        REQUIRE(instructions[ 11] ==     TbInstruction::Comment("// comment 5"));
        REQUIRE(instructions[ 12] ==     TbInstruction::ArrayStart());
        REQUIRE(instructions[ 13] ==       TbInstruction::ArrayStart());
        REQUIRE(instructions[ 14] ==         TbInstruction::Comment("// comment 6"));
        REQUIRE(instructions[ 15] ==       TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 16] ==       TbInstruction::Comment("// comment 7"));
        REQUIRE(instructions[ 17] ==     TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 18] ==     TbInstruction::Comment("// comment 8"));
        REQUIRE(instructions[ 19] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 20] ==   TbInstruction::Key("arr2"));
        REQUIRE(instructions[ 21] ==   TbInstruction::ArrayStart());
        REQUIRE(instructions[ 22] ==     TbInstruction::ArrayStart());
        REQUIRE(instructions[ 23] ==     TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 24] ==     TbInstruction::ArrayStart());
        REQUIRE(instructions[ 25] ==     TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 26] ==     TbInstruction::TableStart());
        REQUIRE(instructions[ 27] ==     TbInstruction::TableEnd());
        REQUIRE(instructions[ 28] ==     TbInstruction::TableStart());
        REQUIRE(instructions[ 29] ==     TbInstruction::TableEnd());
        REQUIRE(instructions[ 30] ==     TbInstruction::Comment("// comment 9"));
        REQUIRE(instructions[ 31] ==   TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 32] ==   TbInstruction::Key("dict1"));
        REQUIRE(instructions[ 33] ==   TbInstruction::TableStart());
        REQUIRE(instructions[ 34] ==     TbInstruction::Key("dict2"));
        REQUIRE(instructions[ 35] ==     TbInstruction::TableStart());
        REQUIRE(instructions[ 36] ==       TbInstruction::Key("arr3"));
        REQUIRE(instructions[ 37] ==       TbInstruction::ArrayStart());
        REQUIRE(instructions[ 38] ==         TbInstruction::Value(1));
        REQUIRE(instructions[ 39] ==         TbInstruction::Value(2));
        REQUIRE(instructions[ 40] ==       TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 41] ==     TbInstruction::TableEnd());
        REQUIRE(instructions[ 42] ==     TbInstruction::Comment("// comment 10"));
        REQUIRE(instructions[ 43] ==   TbInstruction::TableEnd());
        REQUIRE(instructions[ 44] ==   TbInstruction::Key("apa"));
        REQUIRE(instructions[ 45] ==   TbInstruction::Value(6));
        REQUIRE(instructions[ 46] ==   TbInstruction::Comment("// // comment 11"));
        REQUIRE(instructions[ 47] ==   TbInstruction::Key("bepa"));
        REQUIRE(instructions[ 48] ==   TbInstruction::Value(2));
        REQUIRE(instructions[ 49] ==   TbInstruction::Comment("// // comment 12"));
        REQUIRE(instructions[ 50] == TbInstruction::TableEnd());
        REQUIRE(instructions[ 51] == TbInstruction::Key("section_2"));
        REQUIRE(instructions[ 52] == TbInstruction::TableStart());
        REQUIRE(instructions[ 53] ==   TbInstruction::Comment("// comment 13"));
        REQUIRE(instructions[ 54] ==   TbInstruction::Key("apa"));
        REQUIRE(instructions[ 55] ==   TbInstruction::Value(1));
        REQUIRE(instructions[ 56] ==   TbInstruction::Comment("// comment 14"));
        REQUIRE(instructions[ 57] == TbInstruction::TableEnd());
        REQUIRE(instructions[ 58] == TbInstruction::Key("section_3"));
        REQUIRE(instructions[ 59] == TbInstruction::TableStart());
        REQUIRE(instructions[ 60] ==   TbInstruction::Comment("// comment 15"));
        REQUIRE(instructions[ 61] ==   TbInstruction::Key("depa"));
        REQUIRE(instructions[ 62] ==   TbInstruction::TableStart());
        REQUIRE(instructions[ 63] ==     TbInstruction::Key("a"));
        REQUIRE(instructions[ 64] ==     TbInstruction::Value(1));
        REQUIRE(instructions[ 65] ==     TbInstruction::Key("b"));
        REQUIRE(instructions[ 66] ==     TbInstruction::Value(2));
        REQUIRE(instructions[ 67] ==   TbInstruction::TableEnd());
        REQUIRE(instructions[ 68] ==   TbInstruction::Key("bepa"));
        REQUIRE(instructions[ 69] ==   TbInstruction::TableStart());
        REQUIRE(instructions[ 70] ==     TbInstruction::Key("c"));
        REQUIRE(instructions[ 71] ==     TbInstruction::TableStart());
        REQUIRE(instructions[ 72] ==     TbInstruction::TableEnd());
        REQUIRE(instructions[ 73] ==     TbInstruction::Key("d"));
        REQUIRE(instructions[ 74] ==     TbInstruction::ArrayStart());
        REQUIRE(instructions[ 75] ==       TbInstruction::Value(1));
        REQUIRE(instructions[ 76] ==       TbInstruction::Comment("// comment 16"));
        REQUIRE(instructions[ 77] ==       TbInstruction::TableStart());
        REQUIRE(instructions[ 78] ==       TbInstruction::TableEnd());
        REQUIRE(instructions[ 79] ==       TbInstruction::ArrayStart());
        REQUIRE(instructions[ 80] ==       TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 81] ==       TbInstruction::ArrayStart());
        REQUIRE(instructions[ 82] ==         TbInstruction::Value(2));
        REQUIRE(instructions[ 83] ==         TbInstruction::Value(3));
        REQUIRE(instructions[ 84] ==       TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 85] ==       TbInstruction::TableStart());
        REQUIRE(instructions[ 86] ==         TbInstruction::Key("nested"));
        REQUIRE(instructions[ 87] ==         TbInstruction::ArrayStart());
        REQUIRE(instructions[ 88] ==           TbInstruction::Value(4));
        REQUIRE(instructions[ 89] ==           TbInstruction::Value(5));
        REQUIRE(instructions[ 90] ==         TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 91] ==       TbInstruction::TableEnd());
        REQUIRE(instructions[ 92] ==     TbInstruction::ArrayEnd());
        REQUIRE(instructions[ 93] ==     TbInstruction::Key("e"));
        REQUIRE(instructions[ 94] ==     TbInstruction::TableStart());
        REQUIRE(instructions[ 95] ==       TbInstruction::Key("y"));
        REQUIRE(instructions[ 96] ==       TbInstruction::ArrayStart());
        REQUIRE(instructions[ 97] ==         TbInstruction::Value(true));
        REQUIRE(instructions[ 98] ==         TbInstruction::Value(false));
        REQUIRE(instructions[ 99] ==         TbInstruction::Value(true));
        REQUIRE(instructions[100] ==         TbInstruction::Value(false));
        REQUIRE(instructions[101] ==         TbInstruction::Value(""));
        REQUIRE(instructions[102] ==       TbInstruction::ArrayEnd());
        REQUIRE(instructions[103] ==       TbInstruction::Key("f"));
        REQUIRE(instructions[104] ==       TbInstruction::TableStart());
        REQUIRE(instructions[105] ==         TbInstruction::Comment("// comment 17"));
        REQUIRE(instructions[106] ==       TbInstruction::TableEnd());
        REQUIRE(instructions[107] ==     TbInstruction::TableEnd());
        REQUIRE(instructions[108] ==   TbInstruction::TableEnd());
        REQUIRE(instructions[109] == TbInstruction::TableEnd());
        // clang-format on
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

TEST_CASE("value-conversion") {
    SUBCASE("Empty value") {
        // TODO
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

TEST_CASE("value-lookup") {
    SUBCASE("empty root") {
        Value root;

        // TODO: try fewer keys and see if constructor with string is conflicting
        REQUIRE(root.lookup_value_by_path({"does"}) == std::nullopt);
        REQUIRE(root.lookup_value_by_path({"does", "exist"}) == std::nullopt);
        REQUIRE(root.lookup_value_by_path({"does", "not", "exist"}) == std::nullopt);

        REQUIRE(root.lookup_value_by_path("does.not.exist") == std::nullopt);
    }

    SUBCASE("simple") {
        Value root{Value::Table{{{"apa", Value{1}}}}};

        auto result = root.lookup_value_by_path("apa");
        REQUIRE(result != std::nullopt);
        REQUIRE(result->get().is_int());
        REQUIRE(result->get().as_int() == 1);
    }

    SUBCASE("nested") {
        Value root{Value::Table{{{"apa", Value{Value::Table{{"bepa", Value{1}}}}}}}};

        {
            auto result = root.lookup_value_by_path("apa.bepa");
            REQUIRE(result != std::nullopt);
            REQUIRE(result->get().is_int());
            REQUIRE(result->get().as_int() == 1);
        }

        {
            auto result = root.lookup_value_by_path({"apa", "bepa"});
            REQUIRE(result != std::nullopt);
            REQUIRE(result->get().is_int());
            REQUIRE(result->get().as_int() == 1);
        }
    }

    SUBCASE("set values") {
        Value root{Value::Table{{{"apa", Value{Value::Table{{"bepa", Value{1}}}}}}}};

        {
            auto v = Value{2};
            root.set_value_at("apa.bepa", v);

            auto result = root.lookup_value_by_path("apa.bepa");
            REQUIRE(result->get().as_int() == 2);
        }

        {
            auto v = Value{2.0f};
            root.set_value_at("cepa.depa", v);

            auto result = root.lookup_value_by_path("cepa.depa");
            REQUIRE((int) result->get().as_float() == 2);
        }
    }
}