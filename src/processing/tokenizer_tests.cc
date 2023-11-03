#include "tokenizer.hpp"

#include <doctest.h>
#include <fmt/format.h>

#include <string>
#include <vector>

using namespace diffy;

TEST_CASE("tokenizer") {
    SUBCASE("empty") {
        auto a = diffy::tokenize("");
        REQUIRE(a.size() == 0);
    }

    SUBCASE("just_newline") {
        auto line = "\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 1);
        REQUIRE(a[0].str_from(line) == "\n");
    }

    SUBCASE("multiple_newlines") {
        auto line = "apa\nbepa\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 4);
        REQUIRE(a[0].str_from(line) == "apa");
        REQUIRE(a[1].str_from(line) == "\n");
        REQUIRE(a[2].str_from(line) == "bepa");
        REQUIRE(a[3].str_from(line) == "\n");
    }

    SUBCASE("mixed1") {
        auto line = "  apa(bepa \n||  cepa)\t{\r\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 13);
        REQUIRE(a[0].str_from(line) == "  ");
        REQUIRE(a[1].str_from(line) == "apa");
        REQUIRE(a[2].str_from(line) == "(");
        REQUIRE(a[3].str_from(line) == "bepa");
        REQUIRE(a[4].str_from(line) == " ");
        REQUIRE(a[5].str_from(line) == "\n");
        REQUIRE(a[6].str_from(line) == "||");
        REQUIRE(a[7].str_from(line) == "  ");
        REQUIRE(a[8].str_from(line) == "cepa");
        REQUIRE(a[9].str_from(line) == ")");
        REQUIRE(a[10].str_from(line) == "\t");
        REQUIRE(a[11].str_from(line) == "{");
        REQUIRE(a[12].str_from(line) == "\r\n");
    }

    SUBCASE("delimiters") {
        auto line = "a->b.c\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 7);
        REQUIRE(a[0].str_from(line) == "a");
        REQUIRE(a[1].str_from(line) == "-");
        REQUIRE(a[2].str_from(line) == ">");
        REQUIRE(a[3].str_from(line) == "b");
        REQUIRE(a[4].str_from(line) == ".");
        REQUIRE(a[5].str_from(line) == "c");
        REQUIRE(a[6].str_from(line) == "\n");
    }

    SUBCASE("utf8") {
        auto line = "ö->å.ä\n";
        auto a = diffy::tokenize(line);
        REQUIRE(a.size() == 7);
        REQUIRE(a[0].str_from(line) == "ö");
        REQUIRE(a[1].str_from(line) == "-");
        REQUIRE(a[2].str_from(line) == ">");
        REQUIRE(a[3].str_from(line) == "å");
        REQUIRE(a[4].str_from(line) == ".");
        REQUIRE(a[5].str_from(line) == "ä");
        REQUIRE(a[6].str_from(line) == "\n");
    }
}