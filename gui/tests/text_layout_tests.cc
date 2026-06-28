// Tests for the Slint-free display-text logic the diff bridge relies on
// (build_row_model uses expand_for_display for tab/column handling).

#include <doctest.h>

#include "text_layout.hpp"

using namespace diffy::gui;

TEST_CASE("expand_for_display: tab at column 0 fills a full tab stop") {
    int col = 0;
    CHECK(expand_for_display("\t", 4, col) == "    ");
    CHECK(col == 4);
}

TEST_CASE("expand_for_display: tab snaps to the next stop, not a fixed width") {
    int col = 0;
    const std::string out = expand_for_display("ab\tc", 4, col);
    CHECK(out == "ab  c");  // 'ab' -> col 2, tab fills 2 to reach col 4, then 'c'
    CHECK(col == 5);
}

TEST_CASE("expand_for_display: col carries across calls so stops stay aligned") {
    int col = 0;
    expand_for_display("abc", 4, col);  // col -> 3
    CHECK(col == 3);
    const std::string out = expand_for_display("\t", 4, col);
    CHECK(out == " ");  // one space to reach col 4
    CHECK(col == 4);
}

TEST_CASE("expand_for_display: control chars become a single space") {
    int col = 0;
    const std::string out = expand_for_display(std::string("a\x01\x7f" "b"), 4, col);
    CHECK(out == "a  b");
    CHECK(col == 4);
}

TEST_CASE("expand_for_display: a UTF-8 code point counts as one column") {
    int col = 0;
    // "é" is two bytes (0xC3 0xA9) but one display column.
    const std::string out = expand_for_display("\xC3\xA9\t", 4, col);
    CHECK(col == 4);                 // 1 col for é, then tab fills 3 to reach 4
    CHECK(out == "\xC3\xA9" "   ");  // é preserved, then 3 spaces
}

TEST_CASE("expand_for_display: tab_width < 1 is treated as 1") {
    int col = 0;
    CHECK(expand_for_display("\t", 0, col) == " ");
    CHECK(col == 1);
}
