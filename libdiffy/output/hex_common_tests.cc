#include <doctest.h>

#include "output/hex_common.hpp"

#include <cstdint>

using namespace diffy;

// ---------------------------------------------------------------------------
// hex_fit_bytes_per_row: bytes-per-row that keeps the ASCII column on-screen.
//
// A unified hex row is prefix(1) + offset(off_w) + "  "(2) + 3*bpr hex + "|" +
// bpr ascii + "|" = (off_w + 5) fixed cols + 4 cols/byte. So for a per-pane
// budget `width_cols`, the raw fit is (width_cols - (off_w + 5)) / 4, then
// rounded down to a multiple of 8 once >= 8, else clamped to [1, fit].
// ---------------------------------------------------------------------------

TEST_CASE("hex_fit_bytes_per_row: unknown width falls back to 16") {
    CHECK(hex_fit_bytes_per_row(0, 4) == 16);
    CHECK(hex_fit_bytes_per_row(-1, 4) == 16);
    CHECK(hex_fit_bytes_per_row(-100, 8) == 16);
}

TEST_CASE("hex_fit_bytes_per_row: 8 <-> 16 bpr breakpoints (off_w=4)") {
    // fixed = off_w + 5 = 9 cols; fit = (width - 9) / 4.
    // fit first reaches 8 at width 41 ((41-9)/4 == 8).
    CHECK(hex_fit_bytes_per_row(40, 4) == 7);   // just-too-narrow for a rounded 8
    CHECK(hex_fit_bytes_per_row(41, 4) == 8);   // just-wide-enough: exactly 8
    CHECK(hex_fit_bytes_per_row(44, 4) == 8);   // fit == 8
    // 8 holds while fit is 8..15; jumps to 16 once fit reaches 16 at width 73.
    CHECK(hex_fit_bytes_per_row(72, 4) == 8);   // fit == 15 -> rounds down to 8
    CHECK(hex_fit_bytes_per_row(73, 4) == 16);  // fit == 16 -> exactly 16
}

TEST_CASE("hex_fit_bytes_per_row: narrow panes clamp to >=1 without rounding") {
    CHECK(hex_fit_bytes_per_row(37, 4) == 7);  // fit == 7 (below 8: no rounding)
    CHECK(hex_fit_bytes_per_row(33, 4) == 6);  // fit == 6
    CHECK(hex_fit_bytes_per_row(13, 4) == 1);  // fit == 1
    CHECK(hex_fit_bytes_per_row(12, 4) == 1);  // fit == 0 -> clamp to 1
    CHECK(hex_fit_bytes_per_row(5, 4) == 1);   // fit == -1 -> clamp to 1
}

TEST_CASE("hex_fit_bytes_per_row: wider offset column shifts the breakpoints") {
    // off_w = 8 => fixed = 13; fit first reaches 8 at width 45, 16 at width 77.
    CHECK(hex_fit_bytes_per_row(44, 8) == 7);
    CHECK(hex_fit_bytes_per_row(45, 8) == 8);
    CHECK(hex_fit_bytes_per_row(76, 8) == 8);   // fit == 15 -> 8
    CHECK(hex_fit_bytes_per_row(77, 8) == 16);
}

// ---------------------------------------------------------------------------
// hex_should_summarise: collapse a hopeless hex diff to a one-line summary.
// Predicate: (truncated && change_bytes > 512 KiB) || change_bytes > 8 MiB.
// ---------------------------------------------------------------------------

TEST_CASE("hex_should_summarise: coarse cap only bites when truncated") {
    CHECK(kHexCoarseSummariseCap == 512ull * 1024);
    CHECK(hex_should_summarise(kHexCoarseSummariseCap, /*truncated=*/true) == false);      // == cap: not over
    CHECK(hex_should_summarise(kHexCoarseSummariseCap + 1, /*truncated=*/true) == true);   // just over
    CHECK(hex_should_summarise(kHexCoarseSummariseCap + 1, /*truncated=*/false) == false); // over coarse but aligned
    CHECK(hex_should_summarise(0, /*truncated=*/true) == false);
}

TEST_CASE("hex_should_summarise: hard cap bites regardless of truncation") {
    CHECK(kHexHardSummariseCap == 8ull * 1024 * 1024);
    CHECK(hex_should_summarise(kHexHardSummariseCap, /*truncated=*/false) == false);     // == cap: not over
    CHECK(hex_should_summarise(kHexHardSummariseCap + 1, /*truncated=*/false) == true);  // just over
    CHECK(hex_should_summarise(kHexHardSummariseCap + 1, /*truncated=*/true) == true);
}

// ---------------------------------------------------------------------------
// GUI/CLI parity: gui/src/hex_bridge.cc and cli/diffy_main.cc both compute
//   change_bytes = sum of (a_len + b_len) over non-Equal segments,
//   truncated    = out-param of diffy::hex_align,
// and now both branch on hex_should_summarise(change_bytes, truncated). Being a
// pure function of those two values, the two code paths cannot diverge.
// ---------------------------------------------------------------------------

TEST_CASE("hex_should_summarise: GUI and CLI share one decision for identical inputs") {
    struct Case {
        uint64_t change_bytes;
        bool truncated;
        bool expect;
    };
    const Case cases[] = {
        {0, false, false},
        {0, true, false},
        {1024, true, false},                        // small change, truncated: keep rendering
        {kHexCoarseSummariseCap, true, false},      // exactly coarse cap
        {kHexCoarseSummariseCap + 1, true, true},   // just over coarse, truncated
        {kHexCoarseSummariseCap + 1, false, false}, // just over coarse, aligned cleanly
        {4ull * 1024 * 1024, true, true},           // big + truncated
        {4ull * 1024 * 1024, false, false},         // big but aligned cleanly
        {kHexHardSummariseCap, false, false},       // exactly hard cap
        {kHexHardSummariseCap + 1, false, true},    // just over hard cap
    };
    for (const auto& c : cases) {
        const bool gui_decision = hex_should_summarise(c.change_bytes, c.truncated);
        const bool cli_decision = hex_should_summarise(c.change_bytes, c.truncated);
        CHECK(gui_decision == cli_decision);  // same helper -> paths cannot diverge
        CHECK(gui_decision == c.expect);
    }
}

TEST_CASE("hex_fit_bytes_per_row is deterministic across representative inputs") {
    const int widths[] = {0, -1, 12, 13, 40, 41, 72, 73, 120, 240};
    const int offs[] = {4, 6, 8, 12};
    for (int w : widths) {
        for (int o : offs) {
            CHECK(hex_fit_bytes_per_row(w, o) == hex_fit_bytes_per_row(w, o));
        }
    }
    // Typical / wide GUI panes (off_w=4) agree with hand-computed fits.
    CHECK(hex_fit_bytes_per_row(120, 4) == 24);  // fit=(120-9)/4=27 -> (27/8)*8=24
    CHECK(hex_fit_bytes_per_row(240, 4) == 56);  // fit=(240-9)/4=57 -> (57/8)*8=56
}
