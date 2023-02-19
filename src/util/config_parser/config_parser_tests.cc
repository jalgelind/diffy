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
                ctor_args = "\"" + ins.oparg1 + "\"";
            } break;
            case TbOperator::Key: {
                ctor_name = "Key";
                ctor_args = "\"" + ins.oparg1 + "\"";
            } break;
            case TbOperator::TableStart: {
                ctor_name = "TableStart";
                ctor_args = "\"" + ins.oparg1 + "\"";
            } break;
            case TbOperator::TableEnd: {
                ctor_name = "TableEnd";
                ctor_args = "\"" + ins.oparg1 + "\"";
            } break;
            case TbOperator::ArrayStart: {
                ctor_name = "ArrayStart";
                ctor_args = "\"" + ins.oparg1 + "\"";
            } break;
            case TbOperator::ArrayEnd: {
                ctor_name = "ArrayEnd";
                ctor_args = "\"" + ins.oparg1 + "\"";
            } break;
            case TbOperator::Value: {
                switch (ins.oparg2_type) {
                    case TbValueType::None: {
                    ctor_name = "Value";
                    ctor_args = "we don't have None values?";
                    } break;
                    case TbValueType::Int: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("{}", ins.oparg2_int);
                    } break;
                    case TbValueType::Bool: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("{}", ins.oparg2_bool);
                    } break;
                    case TbValueType::String: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("\"{}\"", ins.oparg1);
                    } break;
                    case TbValueType::Float: {
                        ctor_name = "Value";
                        ctor_args = fmt::format("{}", ins.oparg2_float);
                    } break;
                }
            } break;
        }

        auto sdepth = std::string(depth < 0 ? 0 : depth * 2, ' ');
        fmt::print(
            "REQUIRE(instructions[{:>3}] == {}TbInstruction::{}({}));\n",
            index, sdepth, ctor_name, ctor_args);

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
        REQUIRE(instructions[ 0] == TbInstruction { TbOperator::TableStart, "", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 1] == TbInstruction {   TbOperator::Key, "fg", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 2] == TbInstruction {   TbOperator::Value, "white", TbValueType::String, 0, 0, false });
        REQUIRE(instructions[ 3] == TbInstruction {   TbOperator::Key, "bg", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 4] == TbInstruction {   TbOperator::Value, "default", TbValueType::String, 0, 0, false });
        REQUIRE(instructions[ 5] == TbInstruction {   TbOperator::Key, "attr", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 6] == TbInstruction {   TbOperator::ArrayStart, "", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 7] == TbInstruction {     TbOperator::Value, "underline", TbValueType::String, 0, 0, false });
        REQUIRE(instructions[ 8] == TbInstruction {   TbOperator::ArrayEnd, "", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 9] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, 0, false });
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
        REQUIRE(instructions[ 0] == TbInstruction { TbOperator::TableStart, "", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 1] == TbInstruction {   TbOperator::Key, "fg", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 2] == TbInstruction {   TbOperator::Value, "white", TbValueType::String, 0, 0, false });
        REQUIRE(instructions[ 3] == TbInstruction {   TbOperator::Key, "bg", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 4] == TbInstruction {   TbOperator::Value, "default", TbValueType::String, 0, 0, false });
        REQUIRE(instructions[ 5] == TbInstruction {   TbOperator::Key, "attr", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 6] == TbInstruction {   TbOperator::ArrayStart, "", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 7] == TbInstruction {     TbOperator::Value, "underline", TbValueType::String, 0, 0, false });
        REQUIRE(instructions[ 8] == TbInstruction {   TbOperator::ArrayEnd, "", TbValueType::None, 0, 0, false });
        REQUIRE(instructions[ 9] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, 0, false });
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
        REQUIRE(instructions[0] == TbInstruction { TbOperator::Key, "section", (TbValueType) 0, 0, false });
        REQUIRE(instructions[1] == TbInstruction {   TbOperator::TableStart, "from section", (TbValueType) 0, 0, false });
        REQUIRE(instructions[2] == TbInstruction { TbOperator::TableEnd, "", (TbValueType) 0, 0, false });
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
        REQUIRE(instructions.size() == 3);
        REQUIRE(instructions[0] == TbInstruction{TbOperator::Key, "section", (TbValueType) 0, 0, false});
        REQUIRE(instructions[1] ==
                TbInstruction{TbOperator::TableStart, "from section", (TbValueType) 0, 0, false});
        REQUIRE(instructions[2] == TbInstruction{TbOperator::TableEnd, "", (TbValueType) 0, 0, false});
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
        //dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 5);
        REQUIRE(instructions[ 0] == TbInstruction { TbOperator::Comment, "# first comment", TbValueType::None, 0, false });
        REQUIRE(instructions[ 1] == TbInstruction { TbOperator::Comment, "// second comment", TbValueType::None, 0, false });
        REQUIRE(instructions[ 2] == TbInstruction { TbOperator::Key, "section", TbValueType::None, 0, false });
        REQUIRE(instructions[ 3] == TbInstruction { TbOperator::TableStart, "from section", TbValueType::None, 0, false });
        REQUIRE(instructions[ 4] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, false });
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
        REQUIRE(instructions[0] == TbInstruction { TbOperator::Key, "section", (TbValueType) 0, 0, false });
        REQUIRE(instructions[1] == TbInstruction { TbOperator::TableStart, "from section", (TbValueType) 0, 0, false });
        REQUIRE(instructions[2] == TbInstruction { TbOperator::Key, "my_key", (TbValueType) 0, 0, false });
        REQUIRE(instructions[3] == TbInstruction { TbOperator::Value, "hey", (TbValueType) 3, false });
        REQUIRE(instructions[4] == TbInstruction { TbOperator::TableEnd, "", (TbValueType) 0, 0, false });
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

        //cfg_dump_instructions(instructions)

        REQUIRE(result.is_ok());
        // dump_instructions(instructions);
        // clang-format off
        REQUIRE(instructions.size() == 9);
        REQUIRE(instructions[0] == TbInstruction { TbOperator::Key, "section", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[1] == TbInstruction { TbOperator::TableStart, "from section", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[2] == TbInstruction { TbOperator::Key, "my_key", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[3] == TbInstruction { TbOperator::Value, "hey", (TbValueType) 3, 0, 0, false });
        REQUIRE(instructions[4] == TbInstruction { TbOperator::Key, "my_other_key", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[5] == TbInstruction { TbOperator::Value, "1234", (TbValueType) 1, 1234, false });
        REQUIRE(instructions[6] == TbInstruction { TbOperator::Key, "something", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[7] == TbInstruction { TbOperator::Value, "false", (TbValueType) 2, 0, 0, false });
        REQUIRE(instructions[8] == TbInstruction { TbOperator::TableEnd, "", (TbValueType) 0, 0, 0, false });
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
        REQUIRE(instructions[0] == TbInstruction { TbOperator::Key, "section", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[1] == TbInstruction { TbOperator::TableStart, "from section", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[2] == TbInstruction { TbOperator::Key, "my_key", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[3] == TbInstruction { TbOperator::Value, "hey", (TbValueType) 3, 0, 0, false });
        REQUIRE(instructions[4] == TbInstruction { TbOperator::Key, "my_other_key", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[5] == TbInstruction { TbOperator::Value, "1234", (TbValueType) 1, 1234, false });
        REQUIRE(instructions[6] == TbInstruction { TbOperator::Key, "something", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[7] == TbInstruction { TbOperator::Value, "false", (TbValueType) 2, 0, 0, false });
        REQUIRE(instructions[8] == TbInstruction { TbOperator::TableEnd, "", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[9] == TbInstruction { TbOperator::Key, "shapes", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[10] == TbInstruction { TbOperator::TableStart, "from section", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[11] == TbInstruction { TbOperator::Key, "apa", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[12] == TbInstruction { TbOperator::Value, "apa", (TbValueType) 3, 0, 0, false });
        REQUIRE(instructions[13] == TbInstruction { TbOperator::Key, "bepa", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[14] == TbInstruction { TbOperator::Value, "0", (TbValueType) 1, 0, 0, false });
        REQUIRE(instructions[15] == TbInstruction { TbOperator::Key, "cepa", (TbValueType) 0, 0, 0, false });
        REQUIRE(instructions[16] == TbInstruction { TbOperator::Value, "off", (TbValueType) 2, 0, 0, false });
        REQUIRE(instructions[17] == TbInstruction { TbOperator::TableEnd, "", (TbValueType) 0, 0, 0, false });
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
        //dump_instructions(instructions);
        // clang-format off
        auto x = TbInstruction::Value("one");
        REQUIRE(instructions.size() == 21);
        REQUIRE(instructions[  0] == TbInstruction::Key("section"));
        REQUIRE(instructions[  1] == TbInstruction::TableStart("from section"));
        REQUIRE(instructions[  2] ==   TbInstruction::Key("my_key"));
        REQUIRE(instructions[  3] ==   TbInstruction::ArrayStart(""));
        REQUIRE(instructions[  4] ==     TbInstruction::Value(1));
        REQUIRE(instructions[  5] ==     TbInstruction::Value(2));
        REQUIRE(instructions[  6] ==     TbInstruction::Value(3));
        REQUIRE(instructions[  7] ==   TbInstruction::ArrayEnd(""));
        REQUIRE(instructions[  8] ==   TbInstruction::Key("my_other_key"));
        REQUIRE(instructions[  9] ==   TbInstruction::ArrayStart(""));
        REQUIRE(instructions[ 10] ==     TbInstruction::Value("one"));
        REQUIRE(instructions[ 11] ==     TbInstruction::Value("two"));
        REQUIRE(instructions[ 12] ==     TbInstruction::Value("three"));
        REQUIRE(instructions[ 13] ==   TbInstruction::ArrayEnd(""));
        REQUIRE(instructions[ 14] ==   TbInstruction::Key("something"));
        REQUIRE(instructions[ 15] ==   TbInstruction::ArrayStart(""));
        REQUIRE(instructions[ 16] ==     TbInstruction::Value(true));
        REQUIRE(instructions[ 17] ==     TbInstruction::Value(true));
        REQUIRE(instructions[ 18] ==     TbInstruction::Value(false));
        REQUIRE(instructions[ 19] ==   TbInstruction::ArrayEnd(""));
        REQUIRE(instructions[ 20] == TbInstruction::TableEnd(""));
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
        //dump_instructions(instructions);

        // clang-format off
        REQUIRE(instructions.size() == 27);
        REQUIRE(instructions[ 0] == TbInstruction { TbOperator::Comment, "// comment", TbValueType::None, 0, false });
        REQUIRE(instructions[ 1] == TbInstruction { TbOperator::Key, "section", TbValueType::None, 0, false });
        REQUIRE(instructions[ 2] == TbInstruction { TbOperator::TableStart, "from section", TbValueType::None, 0, false });
        REQUIRE(instructions[ 3] == TbInstruction {   TbOperator::Comment, "// comment", TbValueType::None, 0, false });
        REQUIRE(instructions[ 4] == TbInstruction {   TbOperator::Key, "my_key", TbValueType::None, 0, false });
        REQUIRE(instructions[ 5] == TbInstruction {   TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[ 6] == TbInstruction {     TbOperator::Key, "a", TbValueType::None, 0, false });
        REQUIRE(instructions[ 7] == TbInstruction {     TbOperator::Value, "a", TbValueType::String, 0, false });
        REQUIRE(instructions[ 8] == TbInstruction {     TbOperator::Comment, "// another comment", TbValueType::None, 0, false });
        REQUIRE(instructions[ 9] == TbInstruction {   TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[10] == TbInstruction {   TbOperator::Key, "my_other_key", TbValueType::None, 0, false });
        REQUIRE(instructions[11] == TbInstruction {   TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[12] == TbInstruction {     TbOperator::Key, "a", TbValueType::None, 0, false });
        REQUIRE(instructions[13] == TbInstruction {     TbOperator::Value, "1", TbValueType::Int, 1, false });
        REQUIRE(instructions[14] == TbInstruction {     TbOperator::Key, "b", TbValueType::None, 0, false });
        REQUIRE(instructions[15] == TbInstruction {     TbOperator::Value, "false", TbValueType::Bool, 0, false });
        REQUIRE(instructions[16] == TbInstruction {     TbOperator::Key, "c", TbValueType::None, 0, false });
        REQUIRE(instructions[17] == TbInstruction {     TbOperator::Value, "test", TbValueType::String, 0, false });
        REQUIRE(instructions[18] == TbInstruction {     TbOperator::Key, "d", TbValueType::None, 0, false });
        REQUIRE(instructions[19] == TbInstruction {     TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[20] == TbInstruction {       TbOperator::Value, "1", TbValueType::Int, 1, false });
        REQUIRE(instructions[21] == TbInstruction {     TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[22] == TbInstruction {     TbOperator::Comment, "// final comment", TbValueType::None, 0, false });
        REQUIRE(instructions[23] == TbInstruction {     TbOperator::Comment, "// it wasn't", TbValueType::None, 0, false });
        REQUIRE(instructions[24] == TbInstruction {   TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[25] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[26] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, false });
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
        //dump_instructions(instructions);

        // clang-format off
        REQUIRE(instructions.size() == 110);
        REQUIRE(instructions[ 0] == TbInstruction { TbOperator::Comment, "// comment 1", TbValueType::None, 0, false });
        REQUIRE(instructions[ 1] == TbInstruction { TbOperator::Comment, "// comment 2", TbValueType::None, 0, false });
        REQUIRE(instructions[ 2] == TbInstruction { TbOperator::Key, "section_1", TbValueType::None, 0, false });
        REQUIRE(instructions[ 3] == TbInstruction { TbOperator::TableStart, "from section", TbValueType::None, 0, false });
        REQUIRE(instructions[ 4] == TbInstruction {   TbOperator::Comment, "// hello", TbValueType::None, 0, false });
        REQUIRE(instructions[ 5] == TbInstruction {   TbOperator::Key, "arr", TbValueType::None, 0, false });
        REQUIRE(instructions[ 6] == TbInstruction {   TbOperator::Comment, "// comment 3", TbValueType::None, 0, false });
        REQUIRE(instructions[ 7] == TbInstruction {   TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[ 8] == TbInstruction {     TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[ 9] == TbInstruction {     TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[10] == TbInstruction {     TbOperator::Comment, "// comment 4", TbValueType::None, 0, false });
        REQUIRE(instructions[11] == TbInstruction {     TbOperator::Comment, "// comment 5", TbValueType::None, 0, false });
        REQUIRE(instructions[12] == TbInstruction {     TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[13] == TbInstruction {       TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[14] == TbInstruction {         TbOperator::Comment, "// comment 6", TbValueType::None, 0, false });
        REQUIRE(instructions[15] == TbInstruction {       TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[16] == TbInstruction {       TbOperator::Comment, "// comment 7", TbValueType::None, 0, false });
        REQUIRE(instructions[17] == TbInstruction {     TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[18] == TbInstruction {     TbOperator::Comment, "// comment 8", TbValueType::None, 0, false });
        REQUIRE(instructions[19] == TbInstruction {   TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[20] == TbInstruction {   TbOperator::Key, "arr2", TbValueType::None, 0, false });
        REQUIRE(instructions[21] == TbInstruction {   TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[22] == TbInstruction {     TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[23] == TbInstruction {     TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[24] == TbInstruction {     TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[25] == TbInstruction {     TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[26] == TbInstruction {     TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[27] == TbInstruction {     TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[28] == TbInstruction {     TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[29] == TbInstruction {     TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[30] == TbInstruction {     TbOperator::Comment, "// comment 9", TbValueType::None, 0, false });
        REQUIRE(instructions[31] == TbInstruction {   TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[32] == TbInstruction {   TbOperator::Key, "dict1", TbValueType::None, 0, false });
        REQUIRE(instructions[33] == TbInstruction {   TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[34] == TbInstruction {     TbOperator::Key, "dict2", TbValueType::None, 0, false });
        REQUIRE(instructions[35] == TbInstruction {     TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[36] == TbInstruction {       TbOperator::Key, "arr3", TbValueType::None, 0, false });
        REQUIRE(instructions[37] == TbInstruction {       TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[38] == TbInstruction {         TbOperator::Value, "1", TbValueType::Int, 1, false });
        REQUIRE(instructions[39] == TbInstruction {         TbOperator::Value, "2", TbValueType::Int, 2, false });
        REQUIRE(instructions[40] == TbInstruction {       TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[41] == TbInstruction {     TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[42] == TbInstruction {     TbOperator::Comment, "// comment 10", TbValueType::None, 0, false });
        REQUIRE(instructions[43] == TbInstruction {   TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[44] == TbInstruction {   TbOperator::Key, "apa", TbValueType::None, 0, false });
        REQUIRE(instructions[45] == TbInstruction {   TbOperator::Value, "6", TbValueType::Int, 6, false });
        REQUIRE(instructions[46] == TbInstruction {   TbOperator::Comment, "// // comment 11", TbValueType::None, 0, false });
        REQUIRE(instructions[47] == TbInstruction {   TbOperator::Key, "bepa", TbValueType::None, 0, false });
        REQUIRE(instructions[48] == TbInstruction {   TbOperator::Value, "2", TbValueType::Int, 2, false });
        REQUIRE(instructions[49] == TbInstruction {   TbOperator::Comment, "// // comment 12", TbValueType::None, 0, false });
        REQUIRE(instructions[50] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[51] == TbInstruction { TbOperator::Key, "section_2", TbValueType::None, 0, false });
        REQUIRE(instructions[52] == TbInstruction { TbOperator::TableStart, "from section", TbValueType::None, 0, false });
        REQUIRE(instructions[53] == TbInstruction {   TbOperator::Comment, "// comment 13", TbValueType::None, 0, false });
        REQUIRE(instructions[54] == TbInstruction {   TbOperator::Key, "apa", TbValueType::None, 0, false });
        REQUIRE(instructions[55] == TbInstruction {   TbOperator::Value, "1", TbValueType::Int, 1, false });
        REQUIRE(instructions[56] == TbInstruction {   TbOperator::Comment, "// comment 14", TbValueType::None, 0, false });
        REQUIRE(instructions[57] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[58] == TbInstruction { TbOperator::Key, "section_3", TbValueType::None, 0, false });
        REQUIRE(instructions[59] == TbInstruction { TbOperator::TableStart, "from section", TbValueType::None, 0, false });
        REQUIRE(instructions[60] == TbInstruction {   TbOperator::Comment, "// comment 15", TbValueType::None, 0, false });
        REQUIRE(instructions[61] == TbInstruction {   TbOperator::Key, "depa", TbValueType::None, 0, false });
        REQUIRE(instructions[62] == TbInstruction {   TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[63] == TbInstruction {     TbOperator::Key, "a", TbValueType::None, 0, false });
        REQUIRE(instructions[64] == TbInstruction {     TbOperator::Value, "1", TbValueType::Int, 1, false });
        REQUIRE(instructions[65] == TbInstruction {     TbOperator::Key, "b", TbValueType::None, 0, false });
        REQUIRE(instructions[66] == TbInstruction {     TbOperator::Value, "2", TbValueType::Int, 2, false });
        REQUIRE(instructions[67] == TbInstruction {   TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[68] == TbInstruction {   TbOperator::Key, "bepa", TbValueType::None, 0, false });
        REQUIRE(instructions[69] == TbInstruction {   TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[70] == TbInstruction {     TbOperator::Key, "c", TbValueType::None, 0, false });
        REQUIRE(instructions[71] == TbInstruction {     TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[72] == TbInstruction {     TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[73] == TbInstruction {     TbOperator::Key, "d", TbValueType::None, 0, false });
        REQUIRE(instructions[74] == TbInstruction {     TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[75] == TbInstruction {       TbOperator::Value, "1", TbValueType::Int, 1, false });
        REQUIRE(instructions[76] == TbInstruction {       TbOperator::Comment, "// comment 16", TbValueType::None, 0, false });
        REQUIRE(instructions[77] == TbInstruction {       TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[78] == TbInstruction {       TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[79] == TbInstruction {       TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[80] == TbInstruction {       TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[81] == TbInstruction {       TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[82] == TbInstruction {         TbOperator::Value, "2", TbValueType::Int, 2, false });
        REQUIRE(instructions[83] == TbInstruction {         TbOperator::Value, "3", TbValueType::Int, 3, false });
        REQUIRE(instructions[84] == TbInstruction {       TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[85] == TbInstruction {       TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[86] == TbInstruction {         TbOperator::Key, "nested", TbValueType::None, 0, false });
        REQUIRE(instructions[87] == TbInstruction {         TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[88] == TbInstruction {           TbOperator::Value, "4", TbValueType::Int, 4, false });
        REQUIRE(instructions[89] == TbInstruction {           TbOperator::Value, "5", TbValueType::Int, 5, false });
        REQUIRE(instructions[90] == TbInstruction {         TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[91] == TbInstruction {       TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[92] == TbInstruction {     TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[93] == TbInstruction {     TbOperator::Key, "e", TbValueType::None, 0, false });
        REQUIRE(instructions[94] == TbInstruction {     TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[95] == TbInstruction {       TbOperator::Key, "y", TbValueType::None, 0, false });
        REQUIRE(instructions[96] == TbInstruction {       TbOperator::ArrayStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[97] == TbInstruction {         TbOperator::Value, "true", TbValueType::Bool, 0, true });
        REQUIRE(instructions[98] == TbInstruction {         TbOperator::Value, "false", TbValueType::Bool, 0, false });
        REQUIRE(instructions[99] == TbInstruction {         TbOperator::Value, "on", TbValueType::Bool, 0, true });
        REQUIRE(instructions[100] == TbInstruction {         TbOperator::Value, "off", TbValueType::Bool, 0, false });
        REQUIRE(instructions[101] == TbInstruction {         TbOperator::Value, "", TbValueType::String, 0, false });
        REQUIRE(instructions[102] == TbInstruction {       TbOperator::ArrayEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[103] == TbInstruction {       TbOperator::Key, "f", TbValueType::None, 0, false });
        REQUIRE(instructions[104] == TbInstruction {       TbOperator::TableStart, "", TbValueType::None, 0, false });
        REQUIRE(instructions[105] == TbInstruction {         TbOperator::Comment, "// comment 17", TbValueType::None, 0, false });
        REQUIRE(instructions[106] == TbInstruction {       TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[107] == TbInstruction {     TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[108] == TbInstruction {   TbOperator::TableEnd, "", TbValueType::None, 0, false });
        REQUIRE(instructions[109] == TbInstruction { TbOperator::TableEnd, "", TbValueType::None, 0, false });
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
            REQUIRE((int)result->get().as_float() == 2);
        }
    }
}