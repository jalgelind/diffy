#include "processing/context_suggestion.hpp"
#include "util/readlines.hpp"

#include <fmt/format.h>
#include <doctest.h>

#include <iostream>

using namespace diffy;

TEST_CASE("context_suggestion") {
    SUBCASE("simple") {
        std::string s = R"foo(
int a = 0;

void
apa(int bepa, char<3> bop) {
    int a = 0; // adding semicolor breaks the suggestions
    if (a == 0) {
        a = 1;
    }
    else if (a == 1) {
        a = 1;
        a = 0;
    }
}
)foo";

        int line_a_eq_0 = 12;
        std::vector<diffy::Line> lines;
        REQUIRE(diffy::parselines(s, lines, false) == true);

        for (int i = 0; i < lines.size(); i++) {
            auto line = lines[i];
            auto indent = line.indentation_level;
            auto scope = line.scope_level;
            fmt::print("[{:2d} {:2d}] {:2d} '{}' ({})\n", indent, scope, lines[i].line_number, escape_whitespace(line.line), line.line.size());
        }

        std::string suggestion;
    
        for (int i = 0; i < lines.size(); i++) {
            diffy::context_find(lines, i, suggestion);
            fmt::print("suggestion line={}: {}\n", i+1, suggestion);
        }

        diffy::context_find(lines, line_a_eq_0, suggestion);
        fmt::print("suggestion: {}\n", suggestion);
    }

}