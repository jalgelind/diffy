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

namespace {
// Concatenate a visual line's runs back into plain text for assertions.
std::string
line_text(const std::vector<diffy::gui::DisplayRun>& line) {
    std::string s;
    for (const auto& r : line) {
        s += r.text;
    }
    return s;
}
}  // namespace

TEST_CASE("wrap_display_runs: wrap_cols < 1 returns a single concatenated line") {
    std::vector<DisplayRun> runs{{"hello ", 0x11, false}, {"world", 0x22, true}};
    auto lines = wrap_display_runs(runs, 0);
    REQUIRE(lines.size() == 1);
    CHECK(line_text(lines[0]) == "hello world");
    // Styles are preserved as distinct runs.
    REQUIRE(lines[0].size() == 2);
    CHECK(lines[0][1].argb == 0x22);
    CHECK(lines[0][1].bold == true);
}

TEST_CASE("wrap_display_runs: breaks at the last space and drops it") {
    std::vector<DisplayRun> runs{{"alpha beta gamma", 0x1, false}};
    auto lines = wrap_display_runs(runs, 10);  // "alpha beta" is 10 cols
    REQUIRE(lines.size() == 2);
    CHECK(line_text(lines[0]) == "alpha beta");
    CHECK(line_text(lines[1]) == "gamma");
}

TEST_CASE("wrap_display_runs: hard-breaks a word longer than wrap_cols") {
    std::vector<DisplayRun> runs{{"abcdefghij", 0x1, false}};
    auto lines = wrap_display_runs(runs, 4);
    REQUIRE(lines.size() == 3);
    CHECK(line_text(lines[0]) == "abcd");
    CHECK(line_text(lines[1]) == "efgh");
    CHECK(line_text(lines[2]) == "ij");
}

TEST_CASE("wrap_display_runs: style is preserved across a split") {
    // "aaaa" (style A) + "bbbb" (style B); wrap at 4 splits exactly on the seam.
    std::vector<DisplayRun> runs{{"aaaa", 0xAA, false}, {"bbbb", 0xBB, true}};
    auto lines = wrap_display_runs(runs, 4);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].size() == 1);
    CHECK(lines[0][0].argb == 0xAA);
    REQUIRE(lines[1].size() == 1);
    CHECK(lines[1][0].argb == 0xBB);
    CHECK(lines[1][0].bold == true);
}

TEST_CASE("wrap_display_runs: a UTF-8 code point counts as one column") {
    // Four 'é' (2 bytes each) wrap at 2 columns -> two lines of two glyphs.
    std::vector<DisplayRun> runs{{"\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 0x1, false}};
    auto lines = wrap_display_runs(runs, 2);
    REQUIRE(lines.size() == 2);
    CHECK(line_text(lines[0]) == "\xC3\xA9\xC3\xA9");
    CHECK(line_text(lines[1]) == "\xC3\xA9\xC3\xA9");
}

TEST_CASE("wrap_display_runs: empty input yields one empty line") {
    auto lines = wrap_display_runs({}, 10);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].empty());
}
