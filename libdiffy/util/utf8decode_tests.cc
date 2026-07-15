#include "util/utf8decode.hpp"

#include <doctest.h>

#include <iostream>
#include <vector>

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

    SUBCASE("len counts 4-byte code points (emoji)") {
        // 😀 U+1F600 is a single 4-byte code point.
        std::string emoji = "\xF0\x9F\x98\x80";
        REQUIRE(emoji.size() == 4);
        CHECK(utf8_len(emoji) == 1);
        // 'a', 😀, 'b' — 6 bytes, 3 code points.
        std::string mix = "a\xF0\x9F\x98\x80"
                          "b";
        REQUIRE(mix.size() == 6);
        CHECK(utf8_len(mix) == 3);
    }

    SUBCASE("len counts a combining mark as its own code point") {
        // 'e' + combining acute accent U+0301 (0xCC 0x81) renders as one glyph
        // but is two code points (base + mark).
        std::string combining = "e\xCC\x81";
        REQUIRE(combining.size() == 3);
        CHECK(utf8_len(combining) == 2);
    }

    SUBCASE("advance_by steps one code point at a time across mixed widths") {
        // 'a' (1), ö U+00F6 (2), 😀 U+1F600 (4), 'z' (1) — code-point boundaries
        // land at byte offsets 1, 3, 7, 8.
        std::string s = "a\xC3\xB6\xF0\x9F\x98\x80z";
        REQUIRE(s.size() == 8);
        std::vector<std::string::size_type> boundaries;
        for (std::string::size_type i = 0; i < s.size();) {
            i = utf8_advance_by(s, i, 1);
            boundaries.push_back(i);
        }
        CHECK(boundaries == std::vector<std::string::size_type>{1, 3, 7, 8});
    }
}