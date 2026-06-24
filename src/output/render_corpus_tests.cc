// Real-usage rendering tests: drive BOTH renderers over the whole fixture corpus
// (the same path `diffy -s` / `diffy -u file_a file_b` takes) and assert layout
// and well-formedness invariants. This is the render-layer analog of the
// algorithm reconstruct invariant, and it runs under the ASAN build too.
//
// Key column-view invariant: every visual row has the same visible width and
// none exceeds the terminal width — i.e. the panes stay aligned and never
// overflow, for any real input.

#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "output/column_view.hpp"
#include "output/unified.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/readlines.hpp"
#include "util/utf8decode.hpp"

#include <doctest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace diffy;

#ifdef DIFFY_TEST_CASES_DIR

namespace {

// Strip CSI escape sequences (ESC [ ... <final>). The header row always carries a
// trailing reset code even when styling is disabled, so visible width must be
// measured after stripping, exactly as a terminal would render it.
std::string
strip_ansi(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            std::size_t j = i + 2;
            while (j < s.size() && !(s[j] >= '@' && s[j] <= '~'))
                j++;
            i = (j < s.size()) ? j + 1 : j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>>
corpus_pairs() {
    namespace fs = std::filesystem;
    std::vector<std::pair<std::string, std::string>> pairs;
    fs::path root{DIFFY_TEST_CASES_DIR};
    if (!fs::exists(root))
        return pairs;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file())
            continue;
        std::string a = entry.path().string();
        if (a.empty() || a.back() != 'a')
            continue;
        std::string b = a;
        b.back() = 'b';
        if (fs::exists(b))
            pairs.emplace_back(a, b);
    }
    return pairs;
}

}  // namespace

TEST_CASE("column view stays aligned and within width over the whole corpus") {
    const int64_t kWidth = 120;
    int checked = 0;

    for (const auto& [pa, pb] : corpus_pairs()) {
        CAPTURE(pa);
        auto a = readlines(pa, false);
        auto b = readlines(pb, false);
        DiffInput<Line> in{gsl::span<Line>{a}, gsl::span<Line>{b}, pa, pb};
        auto hunks = compose_hunks(Patience<Line>(in).compute().edit_sequence, 3);
        auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);

        // Default ColumnViewState has empty style escape codes, so rows carry no
        // ANSI and utf8_len is the true visible width.
        ColumnViewState config;
        ProgramOptions options;
        auto rows = column_view_render_lines(in, annotated, config, options, kWidth);

        int64_t common_width = -1;
        int64_t max_width = 0;
        bool all_equal = true;
        for (const auto& row : rows) {
            int64_t w = utf8_len(strip_ansi(row));
            if (common_width == -1)
                common_width = w;
            else if (w != common_width)
                all_equal = false;
            if (w > max_width)
                max_width = w;
        }
        REQUIRE(all_equal);            // every pane row is the same width
        REQUIRE(max_width <= kWidth);  // and never overflows the terminal
        checked++;
    }

    MESSAGE("column-view corpus pairs checked: " << checked);
    CHECK(checked > 0);
}

TEST_CASE("unified output is well-formed over the whole corpus") {
    int checked = 0;

    for (const auto& [pa, pb] : corpus_pairs()) {
        CAPTURE(pa);
        auto a = readlines(pa, false);
        auto b = readlines(pb, false);
        DiffInput<Line> in{gsl::span<Line>{a}, gsl::span<Line>{b}, pa, pb};
        auto hunks = compose_hunks(Patience<Line>(in).compute().edit_sequence, 3);
        auto out = unified_diff_render(in, hunks);

        REQUIRE(out.size() >= 2);
        CHECK(out[0].rfind("--- ", 0) == 0);
        CHECK(out[1].rfind("+++ ", 0) == 0);

        bool prefixes_ok = true;
        for (std::size_t i = 2; i < out.size(); i++) {
            char c = out[i].empty() ? '\0' : out[i][0];
            // hunk header '@', or a context/insert/delete body line.
            if (c != '@' && c != ' ' && c != '+' && c != '-')
                prefixes_ok = false;
        }
        REQUIRE(prefixes_ok);
        checked++;
    }

    MESSAGE("unified corpus pairs checked: " << checked);
    CHECK(checked > 0);
}

#endif  // DIFFY_TEST_CASES_DIR
