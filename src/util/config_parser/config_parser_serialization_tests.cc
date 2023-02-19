#include "config_parser.hpp"
#include "config_parser_utils.hpp"
#include "config_serializer.hpp"

#include <doctest.h>

#include <fmt/format.h>

#include <string>
#include <vector>

using namespace diffy;

#if 0
void
dump_test_case(std::vector<TbInstruction> instructions) {
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
            index, std::string(depth < 0 ? 0 : depth * 2, ' '), repr(ins.op), ins.oparg_string,
            repr(ins.oparg_type), ins.oparg_int, ins.oparg_bool);

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
