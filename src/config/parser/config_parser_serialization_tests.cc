#include "config_parser.hpp"
#include "config_parser_utils.hpp"

#include <doctest.h>

#include <fmt/format.h>

#include <string>
#include <vector>

using namespace diffy;

/*
TODO: Use these snippets for testing too

        std::string cfg_text222 = R"foo(
[general]
    apa = { fg='white', attr=[1, 2] }
    bepa = { fg='white', attr=[1, 2] }

[other]
    apa = { fg='white', attr=[1, 2] }

)foo";

        std::string cfg_texeet = R"foo(
[general]
    apa = { fg='white', attr=[1, 2] }

    bepa = 2


)foo";

        std::string cfg_text = R"foo(
            // section_1
            [section_1]
                // my_key
                my_key = {
                    // first one is a
                    // :)
                    first = "a",
                    // second one is b
                    second = "b"
                    // third one is uh?
                }
                my_lock = [0, 1, 2]
                my_other_key = "false"
                my_boo = [
                    // first element
                    { moo = "bass", boo = "dass"},
                    // second element
                    { epa = "bepa" }]
                a = 2
            // empty section
            [empty_section]
                // not empty
                hello = "hello"

            // trailing comment

        )foo";

        std::string cfg_text1 = R"foo(
            // hello
            [section_1]
                empty_dict = {}
                string_dict = { a3 = "a", b3 = "b" }
                integer = 3
                //my_fourth_key = [1, 2, 3]
            [section_2]
                my_key = { a = "b", c = { d = 4 } }
            [section_3]
                bepa = 3
        )foo";

        std::string cfg_text2 = R"foo(
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

        std::string cfg_text_array = R"foo(
            [section]
                my_key = [1, 2, 3]
                my_other_key = ["one", "two", "three"]
                something = [true, on, off]
        )foo";

        // this one crashes cfg_parse_value_tree!
        // std::string cfg_value = "fg='white', bg='default', attr=['underline']";

        std::string cfg_value = "{fg='white', bg='default', attr=['underline']}";

        // { a = 1, b = false, c = 'test', d = [1] }
*/

#if 1
void
dump_instructions2(std::vector<TbInstruction> instructions) {
    int index = 0;
    fmt::print("// clang-format off\n");
    fmt::print("REQUIRE(instructions.size() == {});\n", instructions.size());
    int depth = 0;
    for (auto ins : instructions) {
        if (ins.op == TbOperator::TableEnd || ins.op == TbOperator::ArrayEnd)
            depth--;

        fmt::print(
            "REQUIRE(instructions[{:2}] == TbInstruction {{ {}TbOperator::{}, \"{}\", TbValueType::{}, {}, "
            "{} }});\n",
            index, std::string(depth < 0 ? 0 : depth * 2, ' '), repr(ins.op), ins.oparg1,
            repr(ins.oparg2_type), ins.oparg2_int, ins.oparg2_bool);

        if (ins.op == TbOperator::TableStart || ins.op == TbOperator::ArrayStart)
            depth++;

        index++;
    }
    fmt::print("// clang-format on\n");
}
#endif

TEST_CASE("serializer") {
    SUBCASE("object-starts-with-identifier") {
        std::string cfg_text = "fg='white', bg='default', attr=['underline']";

        diffy::Value value;
        ParseResult result;
        if (!cfg_parse_value_tree(cfg_text, result, value)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());

        // A table wrapper is added, so we can't compare with the input.
        std::string reference = R"foo({
    fg = 'white', bg = 'default', attr = [
        'underline'
    ]
})foo";

        REQUIRE_EQ(cfg_serialize_obj(value), reference);

        // fmt::print("{}\n", cfg_serialize_obj(value));
    }

    SUBCASE("object-starts-with-curly") {
        std::string cfg_text = R"foo({
    fg = 'white', bg = 'default', attr = [
        'underline'
    ]
})foo";

        diffy::Value value;
        ParseResult result;
        if (!cfg_parse_value_tree(cfg_text, result, value)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());

        REQUIRE_EQ(cfg_serialize_obj(value), cfg_text);
    }

    SUBCASE("all-the-comments") {
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

        diffy::Value value;
        ParseResult result;
        if (!cfg_parse_value_tree(cfg_text, result, value)) {
            printf("%s\n", result.error.c_str());
        }
        REQUIRE(result.is_ok());

        // fmt::print("\n{}\n", cfg_serialize(value));

        // REQUIRE_EQ(cfg_serialize_obj(value), cfg_text);
    }
}
