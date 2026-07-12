#include "tokenizer.hpp"

#include <doctest.h>

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

    SUBCASE("form_feed_and_vertical_tab_terminate") {
        // Regression: \f and \v are whitespace but aren't one of the four whitespace
        // chars tokenize handles explicitly, so they used to fall into the word
        // branch (which won't consume whitespace) and spin forever pushing zero-width
        // tokens. Each must be consumed as a single token and the call must return.
        auto ff = " * \ffile\r\n";  // form-feed mid-line, as seen in some C headers
        auto a = diffy::tokenize(ff);
        REQUIRE(a.size() > 0);
        std::string joined;
        for (const auto& t : a) {
            joined += t.str_from(ff);
        }
        REQUIRE(joined == ff);  // every byte accounted for, none duplicated/dropped

        auto vt = "a\vb";
        auto b = diffy::tokenize(vt);
        REQUIRE(b.size() == 3);
        REQUIRE(b[0].str_from(vt) == "a");
        REQUIRE(b[1].str_from(vt) == "\v");
        REQUIRE(b[2].str_from(vt) == "b");
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

TEST_CASE("is_whitespace / is_empty") {
    CHECK(diffy::is_whitespace(' '));
    CHECK(diffy::is_whitespace('\t'));
    CHECK(diffy::is_whitespace('\n'));
    CHECK(diffy::is_whitespace('\r'));
    CHECK_FALSE(diffy::is_whitespace('a'));
    CHECK_FALSE(diffy::is_whitespace('-'));

    CHECK(diffy::is_empty(""));
    CHECK(diffy::is_empty("   \t\r\n"));
    CHECK_FALSE(diffy::is_empty("  x  "));
    CHECK_FALSE(diffy::is_empty("x"));
}