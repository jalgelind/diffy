// Tests for the Slint-free display-text logic the diff bridge relies on
// (build_row_model uses expand_for_display for tab/column handling, and the
// whitespace-glyph helpers for the changed-token band fill).

#include "util/display_text.hpp"

#include <doctest.h>

using namespace diffy;

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

TEST_CASE("expand_for_display: show_ws renders spaces as · and tabs as → + fill") {
    int col = 0;
    // Two spaces -> two middots (·· = C2 B7 C2 B7); columns unchanged (2).
    CHECK(expand_for_display("  ", 4, col, /*show_ws=*/true) == "\xc2\xb7\xc2\xb7");
    CHECK(col == 2);
    // A tab at column 0 -> arrow + 3 fill spaces, still one tab stop (4 columns).
    col = 0;
    CHECK(expand_for_display("\t", 4, col, /*show_ws=*/true) == "\xe2\x86\x92   ");
    CHECK(col == 4);
    // Off by default: identical to the plain expansion.
    col = 0;
    CHECK(expand_for_display("a b", 4, col, /*show_ws=*/false) == "a b");
}

TEST_CASE("display_width: plain ASCII is one column per byte") {
    CHECK(display_width("", 8) == 0);
    CHECK(display_width("hello", 8) == 5);
}

TEST_CASE("display_width: a tab snaps to the next stop, not a fixed width") {
    // Tab from column 0 fills a full stop; from column 2 fills only what remains.
    CHECK(display_width("\t", 8) == 8);
    CHECK(display_width("ab\t", 8) == 8);      // 'ab' -> 2, tab fills 6 to reach 8
    CHECK(display_width("abcdefg\t", 8) == 8); // 7 chars, tab fills 1 to reach 8
    CHECK(display_width("abcdefgh\t", 8) == 16);  // exactly on a stop -> full next stop
    CHECK(display_width("a\tb\t", 8) == 16);   // two tab stops, then 'b' before the 2nd
}

TEST_CASE("display_width: start_col seeds the tab stop for a mid-line fragment") {
    // The same '\t' fragment measured as if it began at column 2 fills only 6.
    CHECK(display_width("\t", 8, 0) == 8);
    CHECK(display_width("\t", 8, 2) == 6);
    CHECK(display_width("\t", 8, 7) == 1);
    CHECK(display_width("\t", 8, 8) == 8);
    // With a 4-wide tab, 'x' at col 3 -> tab fills 1 to reach 4, +1 for x = 2.
    CHECK(display_width("\tx", 4, 3) == 2);
}

TEST_CASE("display_width: a long unbreakable token is exactly its code-point count") {
    CHECK(display_width(std::string(200, 'x'), 8) == 200);
}

TEST_CASE("display_width: a UTF-8 code point counts as one column") {
    // "é" is two bytes (C3 A9) but one column; a tab after it still fills to 8.
    CHECK(display_width("\xC3\xA9", 8) == 1);
    CHECK(display_width("\xC3\xA9\t", 8) == 8);
    // Four 'é' = four columns.
    CHECK(display_width("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 8) == 4);
}

TEST_CASE("display_width: control chars and a lone Latin-1 byte count as one column") {
    // Control bytes each occupy one column (like expand_for_display's single space).
    CHECK(display_width(std::string("a\x01\x7f" "b"), 8) == 4);
    // A stray 0x80 continuation byte counts as one column (tolerant decoder), so
    // the fill width can't desync on mixed-encoding lines.
    CHECK(display_width("\x80", 8) == 1);
    CHECK(display_width("a\x80" "b", 8) == 3);
}

TEST_CASE("display_width: tab_width < 1 is treated as 1") {
    CHECK(display_width("\t", 0) == 1);
    CHECK(display_width("a\tb", 0) == 3);  // every tab advances a single column
}

TEST_CASE("display_width matches expand_for_display's column accounting (valid UTF-8)") {
    // The width function and the expander share one column model: expanding a
    // string and measuring `col` yields the same width display_width reports.
    for (const std::string& s :
         {std::string("hello"), std::string("ab\tc"), std::string("\t"),
          std::string("a\tb\tc"), std::string("\xC3\xA9\t\xC3\xA9"),
          std::string("a\x01\x7f" "b"), std::string("")}) {
        int col = 0;
        expand_for_display(s, 8, col);
        CHECK(col == display_width(s, 8));
    }
}

namespace {
// Concatenate a visual line's runs back into plain text for assertions.
std::string
line_text(const std::vector<diffy::DisplayRun>& line) {
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

TEST_CASE("ws_glyph_at: spaces and the ·/→ display glyphs count as whitespace") {
    CHECK(ws_glyph_at(" ", 0));             // plain space
    CHECK(ws_glyph_at("\xc2\xb7", 0));      // · middot (U+00B7)
    CHECK(ws_glyph_at("\xe2\x86\x92", 0));  // → tab arrow (U+2192)
    CHECK_FALSE(ws_glyph_at("a", 0));       // ordinary glyph
    CHECK_FALSE(ws_glyph_at("\xc3\xa9", 0));  // é — multibyte, not whitespace
    // Detection is positional: a middot lead byte with no continuation doesn't count.
    CHECK_FALSE(ws_glyph_at("\xc2", 0));
}

TEST_CASE("all_ws_glyphs: true only for a non-empty run of whitespace glyphs") {
    CHECK(all_ws_glyphs("   "));                // spaces
    CHECK(all_ws_glyphs("\xc2\xb7\xc2\xb7"));   // ·· two middots
    CHECK(all_ws_glyphs("\xe2\x86\x92 "));      // → then a space
    CHECK_FALSE(all_ws_glyphs(""));             // empty is not "all whitespace"
    CHECK_FALSE(all_ws_glyphs("ab"));           // ordinary text
    CHECK_FALSE(all_ws_glyphs("\xc2\xb7x"));    // middot then a non-ws glyph
}
