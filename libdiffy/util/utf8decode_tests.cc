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

    SUBCASE("malformed byte recovers and counts as one column (TXT-8)") {
        // 'a', an invalid lead byte 0xFF, then 'b' — independently 3 columns. The
        // old DFA stuck in REJECT and counted the tail as width 0 (total 1).
        std::string s = "a\xFF"
                        "b";
        REQUIRE(s.size() == 3);
        CHECK(utf8_len(s) == 3);
    }

    SUBCASE("advance_by is safe on empty input (TXT-8)") {
        std::string empty;
        CHECK(utf8_advance_by(empty, 0, 1) == 0);  // must not underflow to SIZE_MAX
    }
}