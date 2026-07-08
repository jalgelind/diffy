// Tests for unified_diff_render. Uses real temp files because the renderer
// stat()s the file names for timestamps; the timestamp lines themselves are not
// asserted (only their prefixes), keeping the test deterministic.

#include "algorithms/patience.hpp"
#include "output/unified.hpp"
#include "processing/diff_hunk.hpp"
#include "util/readlines.hpp"

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace diffy;

namespace {

std::string
write_temp(const std::string& name, const std::string& content) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary);
    f << content;
    f.close();
    return p.string();
}

bool
contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

TEST_CASE("unified_diff_render — no-newline marker follows the affected side (TXT-5)") {
    // Right file's last line lacks a trailing newline: the marker follows the '+'.
    auto pa = write_temp("diffy_eol_a1.txt", "same\nold\n");
    auto pb = write_temp("diffy_eol_b1.txt", "same\nnew");  // no final newline
    auto A = readlines(pa, false);
    auto B = readlines(pb, false);
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, pa, pb};
    auto hunks = compose_hunks(Patience<Line>(in).compute().edit_sequence, 3);
    auto out = unified_diff_render(in, hunks);

    REQUIRE(contains(out, "\\ No newline at end of file\n"));
    // The marker sits immediately after the "+new" line (the side missing the EOL),
    // never after the "-old" line (A ends with a newline).
    for (std::size_t i = 1; i < out.size(); ++i) {
        if (out[i] == "\\ No newline at end of file\n") {
            CHECK(out[i - 1].rfind("+new", 0) == 0);
        }
    }
}

TEST_CASE("unified_diff_render — no marker when both files end with a newline (TXT-5)") {
    auto pa = write_temp("diffy_eol_a2.txt", "same\nold\n");
    auto pb = write_temp("diffy_eol_b2.txt", "same\nnew\n");
    auto A = readlines(pa, false);
    auto B = readlines(pb, false);
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, pa, pb};
    auto hunks = compose_hunks(Patience<Line>(in).compute().edit_sequence, 3);
    auto out = unified_diff_render(in, hunks);
    CHECK_FALSE(contains(out, "\\ No newline at end of file\n"));
}

TEST_CASE("unified_diff_render") {
    auto pa = write_temp("diffy_unified_a.txt", "line one\nline two\nline three\n");
    auto pb = write_temp("diffy_unified_b.txt", "line one\nline 2\nline three\n");

    auto A = readlines(pa, false);
    auto B = readlines(pb, false);
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, pa, pb};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);

    auto out = unified_diff_render(in, hunks);

    REQUIRE(out.size() >= 4);
    CHECK(out[0].rfind("--- ", 0) == 0);
    CHECK(out[1].rfind("+++ ", 0) == 0);

    bool found_header = false;
    for (const auto& l : out)
        if (l.rfind("@@ -", 0) == 0 && l.find(" +") != std::string::npos)
            found_header = true;
    CHECK(found_header);

    // Context line kept, changed line shown as delete + insert.
    CHECK(contains(out, " line one\n"));
    CHECK(contains(out, "-line two\n"));
    CHECK(contains(out, "+line 2\n"));
    CHECK(contains(out, " line three\n"));
}

TEST_CASE("unified_diff_render — missing file name yields empty output") {
    // get_file_timestamp fails on a non-existent name and the renderer returns
    // empty rather than aborting (regression E2).
    std::vector<Line> empty;
    DiffInput<Line> in{gsl::span<Line>{empty}, gsl::span<Line>{empty}, "/diffy/definitely/missing_a",
                       "/diffy/definitely/missing_b"};
    auto out = unified_diff_render(in, {});
    CHECK(out.empty());
}
