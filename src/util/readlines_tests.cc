#include "util/readlines.hpp"

#include <fmt/format.h>
#include <doctest.h>

#include <iostream>

using namespace diffy;

TEST_CASE("readlines") {
    SUBCASE("just_text") {
        std::string s = "öl\nbål\nskur";
        std::vector<diffy::Line> lines;

        REQUIRE(diffy::parselines(s, lines, false) == true);

        REQUIRE(lines.size() == 3);
        REQUIRE(lines[0].line == "öl\n");
        REQUIRE(lines[1].line == "bål\n");
        REQUIRE(lines[2].line == "skur");

    }

    SUBCASE("simple") {
        std::string s = R"foo(void
apa() {
    int a = 0
    if (a == 0) {
        a = 1;
    }
    else if (a == 1) {
        a = 0;
    }
}
)foo";
        std::vector<diffy::Line> lines;

        REQUIRE(diffy::parselines(s, lines, false) == true);
#if 0
        for (int i = 0; i < lines.size(); i++) {
            auto line = lines[i];
            auto indent = line.indentation_level;
            auto scope = line.scope_level;
            //fmt::print("[{:2d} {:2d}] '{}' ({})\n", indent, scope, escape_whitespace(line.line), line.line.size());
            fmt::print("REQUIRE(lines[{}].indentation_level == {});\n", i, indent);
            fmt::print("REQUIRE(lines[{}].scope_level       == {});\n", i, scope);
            fmt::print("REQUIRE(lines[{}].text              == \"{}\");\n", i, escape_whitespace(line.line));
        }
#endif
        REQUIRE(lines[0].indentation_level == 0);
        REQUIRE(lines[0].scope_level       == 0);
        REQUIRE(lines[0].line              == "void\n");
        REQUIRE(lines[1].indentation_level == 0);
        REQUIRE(lines[1].scope_level       == 0);
        REQUIRE(lines[1].line              == "apa() {\n");
        REQUIRE(lines[2].indentation_level == 4);
        REQUIRE(lines[2].scope_level       == 1);
        REQUIRE(lines[2].line              == "    int a = 0\n");
        REQUIRE(lines[3].indentation_level == 4);
        REQUIRE(lines[3].scope_level       == 1);
        REQUIRE(lines[3].line              == "    if (a == 0) {\n");
        REQUIRE(lines[4].indentation_level == 8);
        REQUIRE(lines[4].scope_level       == 2);
        REQUIRE(lines[4].line              == "        a = 1;\n");
        REQUIRE(lines[5].indentation_level == 4);
        REQUIRE(lines[5].scope_level       == 1);
        REQUIRE(lines[5].line              == "    }\n");
        REQUIRE(lines[6].indentation_level == 4);
        REQUIRE(lines[6].scope_level       == 1);
        REQUIRE(lines[6].line              == "    else if (a == 1) {\n");
        REQUIRE(lines[7].indentation_level == 8);
        REQUIRE(lines[7].scope_level       == 2);
        REQUIRE(lines[7].line              == "        a = 0;\n");
        REQUIRE(lines[8].indentation_level == 4);
        REQUIRE(lines[8].scope_level       == 1);
        REQUIRE(lines[8].line              == "    }\n");
        REQUIRE(lines[9].indentation_level == 0);
        REQUIRE(lines[9].scope_level       == 0);
        REQUIRE(lines[9].line              == "}\n");
        REQUIRE(lines[10].indentation_level == 0);
        REQUIRE(lines[10].scope_level       == 0);
        REQUIRE(lines[10].line              == "");
    }

}