// Tests for the side-by-side column view. A default-constructed ColumnViewState
// has empty style escape codes, so the rendered rows contain no ANSI sequences
// and can be asserted directly. column_view_render_lines takes an explicit width
// so the output is deterministic.

#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "output/column_view.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/hash.hpp"
#include "util/readlines.hpp"

#include <doctest.h>

#include <string>
#include <vector>

using namespace diffy;

namespace {

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
render_cv(const std::vector<std::string>& a,
          const std::vector<std::string>& b,
          ColumnViewState& config,
          int64_t width) {
    auto A = mk(a);
    auto B = mk(b);
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "LEFTNAME", "RIGHTNAME"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);
    ProgramOptions options;
    return column_view_render_lines(in, annotated, config, options, width);
}

bool
any_contains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& l : lines)
        if (l.find(needle) != std::string::npos)
            return true;
    return false;
}

}  // namespace

TEST_CASE("column_view_render_lines — basic layout") {
    ColumnViewState config;
    auto lines = render_cv({"alpha", "beta", "gamma"}, {"alpha", "BETA", "gamma"}, config, 80);

    // File-name header, a git-style hunk header (@@ …), then three content rows
    // (alpha / beta<->BETA / gamma).
    REQUIRE(lines.size() == 5);

    // The file-name header carries both file names.
    CHECK(lines[0].find("LEFTNAME") != std::string::npos);
    CHECK(lines[0].find("RIGHTNAME") != std::string::npos);

    // A git-style hunk header line is present.
    CHECK(any_contains(lines, "@@ -"));

    // Every row except the full-width hunk header has the column separator
    // between the two panes.
    for (const auto& l : lines)
        if (l.rfind("@@ ", 0) != 0)
            CHECK(l.find("│") != std::string::npos);

    // Content from both sides is present.
    CHECK(any_contains(lines, "alpha"));
    CHECK(any_contains(lines, "beta"));
    CHECK(any_contains(lines, "BETA"));
    CHECK(any_contains(lines, "gamma"));
}

TEST_CASE("column_view_render_lines — hunk starting with >=2 deletes stays aligned (TXT-2)") {
    // A changed region beginning with two deletions must pad the right pane with
    // two blank rows so the following common lines stay aligned. The old aligner
    // underflowed its unsigned counter here, dropped a padding row, and shifted
    // every later row up one (so "del2" landed opposite "keep1").
    ColumnViewState config;
    auto lines = render_cv({"del1", "del2", "keep1", "keep2"}, {"keep1", "keep2"}, config, 80);

    // Split a content row into (left, right) panes on the column separator.
    auto panes = [](const std::string& row) -> std::pair<std::string, std::string> {
        const std::string sep = "│";
        auto p = row.find(sep);
        if (p == std::string::npos)
            return {row, ""};
        return {row.substr(0, p), row.substr(p + sep.size())};
    };

    bool saw_del2 = false;
    for (const auto& l : lines) {
        auto [lp, rp] = panes(l);
        if (lp.find("del2") != std::string::npos) {
            saw_del2 = true;
            CHECK(rp.find("keep") == std::string::npos);  // right pane is blank, not misaligned
        }
        if (lp.find("keep1") != std::string::npos) {
            CHECK(rp.find("keep1") != std::string::npos);  // common line aligned across panes
        }
    }
    CHECK(saw_del2);
}

TEST_CASE("column_view_render_lines — word wrap produces more rows than chopping") {
    std::string long_line(120, 'x');
    ColumnViewState wrap;
    wrap.settings.word_wrap = true;
    ColumnViewState chop;
    chop.settings.word_wrap = false;

    auto wrapped = render_cv({"short"}, {long_line}, wrap, 80);
    auto chopped = render_cv({"short"}, {long_line}, chop, 80);

    CHECK(wrapped.size() > chopped.size());
}

TEST_CASE("column_view_render_lines — narrow width is clamped and does not crash") {
    ColumnViewState config;
    auto lines = render_cv({"hello world"}, {"goodbye world"}, config, 10);
    REQUIRE(!lines.empty());
    // Every row except the full-width hunk header keeps the column separator.
    for (const auto& l : lines)
        if (l.rfind("@@ ", 0) != 0)
            CHECK(l.find("│") != std::string::npos);
}
