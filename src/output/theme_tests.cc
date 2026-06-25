// Inverted / background-filled theme tests for the column view.
//
// Unlike column_view_tests.cc (which uses a default, fg-only ColumnViewState so
// the output is ANSI-free), these tests install a theme that sets a *background*
// on every style key and then assert on the emitted escape codes: a fully
// inverted theme must paint every visible cell with a configured background —
// deletion/insertion bars spanning the full column width, no terminal-default
// gaps. The ANSI oracle (bg_runs/has_default_bg) and theme factories live in
// render_test_util.hpp. See the "Theme / inverted color-scheme roadmap" in
// ROADMAP.md (TH-1..6, TH-T0..T5).

#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "output/column_view.hpp"
#include "output/render_test_util.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/color.hpp"
#include "util/hash.hpp"
#include "util/readlines.hpp"

#include <doctest.h>

#include <string>
#include <vector>

using namespace diffy;
using namespace diffy::test;

namespace {

// Distinct palette ids, named for readability in assertions. Must match the
// 256-color ids inverted_theme() assigns in render_test_util.hpp.
const std::string kDeleteBg = "8:52";
const std::string kInsertBg = "8:22";
const std::string kCommonBg = "8:236";
const std::string kCommonNumBg = "8:235";
const std::string kDeleteTokenFg = "38;5;88";  // raw fg escape fragment
const std::string kInsertTokenFg = "38;5;28";

std::vector<Line>
mk(const std::vector<std::string>& v) {
    std::vector<Line> out;
    uint32_t i = 1;
    for (const auto& s : v) {
        out.push_back(Line{i, hash::hash(s.c_str(), s.size()), s});
        i++;
    }
    return out;
}

std::vector<std::string>
render(const std::vector<std::string>& a,
       const std::vector<std::string>& b,
       ColumnViewState& config,
       int64_t width = 80) {
    auto A = mk(a);
    auto B = mk(b);
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "LEFTNAME", "RIGHTNAME"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);
    ProgramOptions options;
    return column_view_render_lines(in, annotated, config, options, width);
}

// First row whose visible (ANSI-stripped) text contains `needle`.
std::string
row_with(const std::vector<std::string>& rows, const std::string& needle) {
    for (const auto& r : rows)
        if (strip_ansi(r).find(needle) != std::string::npos)
            return r;
    return "";
}

}  // namespace

// ---- TH-T0 self-check: the oracle behaves -----------------------------------

TEST_CASE("bg_runs tracks SGR backgrounds and default gaps") {
    // "AB" with no styling -> one default run.
    CHECK(has_default_bg("AB"));
    CHECK(bg_runs("AB").size() == 1);
    CHECK(bg_runs("AB")[0].first == "");

    // 256-color bg around text, then reset: one non-default run, no default run.
    std::string styled = "\033[48;5;52mXY\033[0m";
    CHECK_FALSE(has_default_bg(styled));
    REQUIRE(bg_runs(styled).size() == 1);
    CHECK(bg_runs(styled)[0].first == "8:52");
    CHECK(bg_runs(styled)[0].second == 2);

    // Truecolor bg is recognized too.
    std::string tc = "\033[48;2;28;28;28mZ\033[0m";
    CHECK_FALSE(has_default_bg(tc));
    CHECK(bg_set(tc).count("24:28,28,28") == 1);

    // A foreground change must not be mistaken for a background.
    std::string fg_only = "\033[38;5;88;1mZ\033[0m";
    CHECK(has_default_bg(fg_only));  // fg set, bg still default

    // A bg followed by a fg keeps the bg active under the fg'd text.
    std::string layered = "\033[48;5;52m\033[38;5;88;1mQ\033[0m";
    CHECK_FALSE(has_default_bg(layered));
    CHECK(bg_set(layered).count("8:52") == 1);
}

// ---- TH-T1: full-width deletion bar (TH-1 regression) -----------------------

TEST_CASE("inverted theme: deletion fills the full column width, not just the text") {
    auto config = inverted_theme();
    auto rows = render({"common1", "DEL", "common2"}, {"common1", "common2"}, config);

    auto del_row = row_with(rows, "DEL");
    REQUIRE_FALSE(del_row.empty());

    auto bgs = bg_set(del_row);
    // The deletion row's padding now carries the delete background...
    CHECK(bgs.count(kDeleteBg) == 1);
    // ...and the common-line background does NOT leak into the deleted row.
    // (Before TH-1 the trailing padding was painted common_line.)
    CHECK(bgs.count(kCommonBg) == 0);
    CHECK(bgs.count(kCommonNumBg) == 0);
}

// ---- TH-T2: no terminal-default gaps anywhere (TH-1 + TH-3) -----------------

TEST_CASE("inverted theme: every row is fully painted, no default-bg gaps") {
    const std::vector<std::string> a = {"alpha", "beta", "gamma", "delta"};
    const std::vector<std::string> b = {"alpha", "BETA", "gamma", "epsilon", "delta"};

    SUBCASE("context-colored line numbers") {
        auto config = inverted_theme();
        config.settings.context_colored_line_numbers = true;
        auto rows = render(a, b, config);
        REQUIRE(!rows.empty());
        for (const auto& row : rows) {
            CAPTURE(strip_ansi(row));
            CHECK_FALSE(has_default_bg(row));
        }
    }

    SUBCASE("uncolored line numbers (TH-3 path)") {
        auto config = inverted_theme();
        config.settings.context_colored_line_numbers = false;
        auto rows = render(a, b, config);
        REQUIRE(!rows.empty());
        for (const auto& row : rows) {
            CAPTURE(strip_ansi(row));
            CHECK_FALSE(has_default_bg(row));
        }
    }

    SUBCASE("truecolor backgrounds (hex theme path)") {
        auto config = inverted_theme_truecolor();
        auto rows = render(a, b, config);
        REQUIRE(!rows.empty());
        bool saw_truecolor = false;
        for (const auto& row : rows) {
            CAPTURE(strip_ansi(row));
            CHECK_FALSE(has_default_bg(row));
            for (const auto& [bg, len] : bg_runs(row)) {
                (void) len;
                if (bg.rfind("24:", 0) == 0)
                    saw_truecolor = true;
            }
        }
        CHECK(saw_truecolor);  // the 48;2 path was actually exercised
    }
}

// ---- TH-T3: whole-line tint + token layering (TH-2 / TH-5) ------------------

TEST_CASE("inverted theme: changed line is tinted whole, token highlight layered on top") {
    auto config = inverted_theme();
    auto rows = render({"hello world"}, {"hello WORLD"}, config);

    auto row = row_with(rows, "hello");
    REQUIRE_FALSE(row.empty());

    auto bgs = bg_set(row);
    // The unchanged "hello " segment of the deleted line gets the delete-line
    // background (TH-2), and the insert side gets the insert-line background —
    // so the common-line background appears nowhere on a fully-changed row.
    CHECK(bgs.count(kDeleteBg) == 1);
    CHECK(bgs.count(kInsertBg) == 1);
    CHECK(bgs.count(kCommonBg) == 0);

    // The token highlights still render — their foregrounds are present, layered
    // over the line background (the token cells carry no bg of their own, TH-5).
    CHECK(row.find(kDeleteTokenFg) != std::string::npos);
    CHECK(row.find(kInsertTokenFg) != std::string::npos);
}

// ---- TH-T5: single background knob inherited by every cell (TH-4) -----------

TEST_CASE("style.background alone fills every cell") {
    ColumnViewState config;
    config.style.background = ansi(TermColor::kWhite, pal(232));  // only the knob

    auto rows = render({"alpha", "beta", "gamma"}, {"alpha", "BETA", "gamma"}, config);
    REQUIRE(!rows.empty());

    for (const auto& row : rows) {
        CAPTURE(strip_ansi(row));
        CHECK_FALSE(has_default_bg(row));
        // With no per-key styles set, the only background in play is the knob.
        for (const auto& [bg, len] : bg_runs(row)) {
            (void) len;
            CHECK(bg == "8:232");
        }
    }
}
