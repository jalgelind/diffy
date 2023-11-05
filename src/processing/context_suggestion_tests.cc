#include "processing/context_suggestion.hpp"

#include <fmt/format.h>
#include <doctest.h>

#include <iostream>

using namespace diffy;

TEST_CASE("context_suggestion") {
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
        //std::vector<diffy::Line> lines;

        //REQUIRE(diffy::parselines(s, lines, false) == true);

    }

}