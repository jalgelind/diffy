#include "util/utf8decode.hpp"

#include <doctest.h>

#include <iostream>

using namespace diffy;

TEST_CASE("unicode") {
    SUBCASE("len2") {
        std::string s = "öl och bål";
        REQUIRE(utf8_len(s, 0, s.size()) == 10);
    }

    SUBCASE("len3") {
        std::string s = "öl och bål";
        REQUIRE(utf8_len(s) == 10);
    }

    SUBCASE("offset") {
        std::string s = "öl och bål";
        auto offset = utf8_advance_by(s, 0, 9);
        REQUIRE(offset == 11);
        REQUIRE(s.substr(offset, 1) == "l");
    }
}