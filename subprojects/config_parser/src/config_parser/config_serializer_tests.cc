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

TEST_CASE("string escaping") {
    // Round-trip a string value through serialize -> parse and return what came back.
    auto roundtrip = [](const std::string& s) -> std::string {
        diffy::Value v;  // default-constructs to an empty table
        v["k"] = diffy::Value{s};
        const std::string text = cfg_serialize_obj(v);
        diffy::Value parsed;
        ParseResult r;
        REQUIRE(cfg_parse_value_tree(text, r, parsed));
        REQUIRE(r.is_ok());
        REQUIRE(parsed.is_table());
        REQUIRE(parsed.contains("k"));
        return parsed["k"].as_string();
    };

    SUBCASE("clean strings stay single-quoted literals") {
        diffy::Value v;
        v["k"] = diffy::Value{std::string("hello world")};
        REQUIRE(cfg_serialize_obj(v).find("'hello world'") != std::string::npos);
    }
    SUBCASE("strings needing escapes use double quotes") {
        diffy::Value v;
        v["k"] = diffy::Value{std::string("it's")};
        REQUIRE(cfg_serialize_obj(v).find("\"it's\"") != std::string::npos);
    }

    SUBCASE("roundtrip double quotes") { REQUIRE_EQ(roundtrip("say \"hi\""), "say \"hi\""); }
    SUBCASE("roundtrip single quote") { REQUIRE_EQ(roundtrip("it's a test"), "it's a test"); }
    SUBCASE("roundtrip backslash") { REQUIRE_EQ(roundtrip("a\\b\\c"), "a\\b\\c"); }
    SUBCASE("roundtrip newline and tab") {
        REQUIRE_EQ(roundtrip("line1\nline2\tend"), "line1\nline2\tend");
    }
    SUBCASE("roundtrip everything at once") {
        REQUIRE_EQ(roundtrip("q'\"\\\n\tz"), "q'\"\\\n\tz");
    }
    SUBCASE("roundtrip empty") { REQUIRE_EQ(roundtrip(""), ""); }

    SUBCASE("back-compat: single-quoted literals keep backslashes verbatim") {
        // Existing configs store Windows paths as raw single-quoted literals; the
        // backslashes must NOT be treated as escapes (\t etc.).
        diffy::Value parsed;
        ParseResult r;
        REQUIRE(cfg_parse_value_tree("{ k = 'C:\\node\\temp' }", r, parsed));
        REQUIRE(r.is_ok());
        REQUIRE_EQ(parsed["k"].as_string(), "C:\\node\\temp");
    }

    SUBCASE("array of tables round-trips (with an escaped string)") {
        diffy::Value parsed;
        ParseResult r;
        REQUIRE(cfg_parse_value_tree("{ arr = [{ a = 1, b = 'x' }, { a = 2, b = \"y\\nz\" }] }", r,
                                     parsed));
        REQUIRE(r.is_ok());
        REQUIRE(parsed["arr"].is_array());
        REQUIRE_EQ(parsed["arr"][0]["b"].as_string(), "x");
        REQUIRE_EQ(parsed["arr"][1]["b"].as_string(), "y\nz");
    }

    SUBCASE("pr_cache-shaped structure round-trips through serialize->parse") {
        // Mirrors what pr_cache writes: a section holding an array of repo entries,
        // each with a PR array whose descriptions carry quotes/newlines + nested
        // reviewer tables.
        diffy::Value pr;
        pr["title"] = diffy::Value{std::string("Fix it's \"bug\"")};
        pr["description"] = diffy::Value{std::string("line1\nline2 with 'quote' and \\slash")};
        diffy::Value reviewers{diffy::Value::Array{}};
        diffy::Value rv;
        rv["name"] = diffy::Value{std::string("Alice")};
        rv["approved"] = diffy::Value{true};
        reviewers.as_array().push_back(rv);
        pr["reviewers"] = reviewers;

        diffy::Value prs{diffy::Value::Array{}};
        prs.as_array().push_back(pr);
        diffy::Value entry;
        entry["key"] = diffy::Value{std::string("workspace/repo")};
        entry["prs"] = prs;
        diffy::Value repos{diffy::Value::Array{}};
        repos.as_array().push_back(entry);
        diffy::Value section;
        section["repos"] = repos;
        diffy::Value root;
        root["pr_cache"] = section;

        const std::string text = cfg_serialize(root);
        diffy::Value parsed;
        ParseResult r;
        REQUIRE(cfg_parse_value_tree(text, r, parsed));
        REQUIRE(r.is_ok());
        auto reposv = parsed.lookup_value_by_path("pr_cache.repos");
        REQUIRE(reposv);
        auto& arr = reposv->get().as_array();
        REQUIRE_EQ(arr.size(), 1);
        REQUIRE_EQ(arr[0]["key"].as_string(), "workspace/repo");
        auto& p0 = arr[0]["prs"].as_array()[0];
        REQUIRE_EQ(p0["title"].as_string(), "Fix it's \"bug\"");
        REQUIRE_EQ(p0["description"].as_string(), "line1\nline2 with 'quote' and \\slash");
        REQUIRE_EQ(p0["reviewers"].as_array()[0]["name"].as_string(), "Alice");
        REQUIRE_EQ(p0["reviewers"].as_array()[0]["approved"].as_bool(), true);
    }
}
