// Correctness tests for the three diff algorithms.
//
// The core oracle is an *invariant*, not a golden: an edit script is a valid
// transformation of A into B iff
//   (1) the non-deleted edits enumerate B[0..M-1] in order,
//   (2) the non-inserted edits enumerate A[0..N-1] in order, and
//   (3) every Common edit pairs equal lines.
// This holds for any valid diff regardless of minimality, so it lets us check
// all three algorithms (and the whole fixture corpus) without expected output.

#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "util/hash.hpp"
#include "util/readlines.hpp"

#include <doctest.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace diffy;

namespace {

std::vector<Line>
make_lines(const std::vector<std::string>& strs) {
    std::vector<Line> out;
    uint32_t i = 1;
    for (const auto& s : strs) {
        out.push_back(Line{i, hash::hash(s.c_str(), s.size()), s});
        i++;
    }
    return out;
}

// One Line per character; convenient for compact cases.
std::vector<Line>
lines(const std::string& chars) {
    std::vector<std::string> v;
    for (char c : chars)
        v.emplace_back(1, c);
    return make_lines(v);
}

// "L0".."L{n-1}"
std::vector<std::string>
seq(int n) {
    std::vector<std::string> v;
    v.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i++)
        v.push_back("L" + std::to_string(i));
    return v;
}

// Returns true iff `r` is a valid edit script transforming A into B.
// Builds a single boolean so corpus files don't emit millions of assertions.
bool
is_valid_transform(const std::vector<Line>& A, const std::vector<Line>& B, const DiffResult& r) {
    if (r.status == DiffResultStatus::Failed)
        return false;

    // Some algorithms return an empty sequence for "no changes"; others return
    // an all-Common sequence. An empty sequence means A and B must be identical.
    if (r.edit_sequence.empty()) {
        if (A.size() != B.size())
            return false;
        for (size_t i = 0; i < A.size(); i++)
            if (A[i].line != B[i].line)
                return false;
        return true;
    }

    std::vector<int64_t> a_indices;
    std::vector<int64_t> b_indices;
    for (const auto& e : r.edit_sequence) {
        if (e.type != EditType::Insert)
            a_indices.push_back(e.a_index.value);
        if (e.type != EditType::Delete)
            b_indices.push_back(e.b_index.value);
        if (e.type == EditType::Common) {
            if (e.a_index.value < 0 || e.b_index.value < 0 ||
                e.a_index.value >= static_cast<int64_t>(A.size()) ||
                e.b_index.value >= static_cast<int64_t>(B.size()) ||
                A[e.a_index.value].line != B[e.b_index.value].line)
                return false;
        }
    }

    if (a_indices.size() != A.size() || b_indices.size() != B.size())
        return false;
    for (size_t i = 0; i < a_indices.size(); i++)
        if (a_indices[i] != static_cast<int64_t>(i))
            return false;
    for (size_t i = 0; i < b_indices.size(); i++)
        if (b_indices[i] != static_cast<int64_t>(i))
            return false;
    return true;
}

void
check_all_algos(const std::vector<Line>& a_in, const std::vector<Line>& b_in, bool include_greedy = true) {
    if (include_greedy) {
        std::vector<Line> A = a_in, B = b_in;
        DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
        INFO("algorithm = myers-greedy");
        REQUIRE(is_valid_transform(A, B, MyersGreedy<Line>(in).compute()));
    }
    {
        std::vector<Line> A = a_in, B = b_in;
        DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
        INFO("algorithm = myers-linear");
        REQUIRE(is_valid_transform(A, B, MyersLinear<Line>(in).compute()));
    }
    {
        std::vector<Line> A = a_in, B = b_in;
        DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
        INFO("algorithm = patience");
        REQUIRE(is_valid_transform(A, B, Patience<Line>(in).compute()));
    }
}

}  // namespace

TEST_CASE("diff algorithms produce valid edit scripts") {
    SUBCASE("classic myers example") {
        check_all_algos(lines("abcabba"), lines("cbabac"));
    }
    SUBCASE("identical") {
        check_all_algos(lines("abc"), lines("abc"));
    }
    SUBCASE("both empty") {
        check_all_algos(lines(""), lines(""));
    }
    SUBCASE("all insert") {
        check_all_algos(lines(""), lines("abc"));
    }
    SUBCASE("all delete") {
        check_all_algos(lines("abc"), lines(""));
    }
    SUBCASE("single substitution") {
        check_all_algos(lines("abc"), lines("axc"));
    }
    SUBCASE("prepend") {
        check_all_algos(lines("abcde"), lines("xabcde"));
    }
    SUBCASE("append") {
        check_all_algos(lines("abcde"), lines("abcdex"));
    }
    SUBCASE("full replacement") {
        check_all_algos(lines("aaaa"), lines("bbbb"));
    }
}

// Both Myers variants compute a *minimal* edit script, so for the same input
// they must agree on the edit distance (number of non-common edits). The
// reconstruct invariant only checks validity, so this guards against a
// regression that produces valid-but-bloated diffs. (Patience is intentionally
// allowed to be suboptimal, so it is excluded.)
TEST_CASE("Myers greedy and linear agree on edit distance") {
    auto edit_distance = [](const DiffResult& r) {
        int n = 0;
        for (const auto& e : r.edit_sequence)
            n += e.type != EditType::Common;
        return n;
    };

    const std::vector<std::pair<std::string, std::string>> cases = {
        {"abcabba", "cbabac"}, {"abc", "axc"},   {"abcde", "xabcde"},
        {"abcde", "abcdex"},   {"aaaa", "bbbb"}, {"the quick fox", "the slow fox"},
        {"", "abc"},           {"abc", ""},      {"hello world", "hello there world"},
    };
    for (const auto& [sa, sb] : cases) {
        CAPTURE(sa);
        CAPTURE(sb);
        std::vector<Line> A = lines(sa), B = lines(sb);
        DiffInput<Line> ig{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
        DiffInput<Line> il{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
        auto greedy = MyersGreedy<Line>(ig).compute();
        auto linear = MyersLinear<Line>(il).compute();
        REQUIRE(is_valid_transform(A, B, greedy));
        REQUIRE(is_valid_transform(A, B, linear));
        CHECK(edit_distance(greedy) == edit_distance(linear));
    }
}

// Pins the index-type selection in MyersGreedy::diff() (uint8 < 127, uint16 <
// 32767, then uint32) — the class of bug behind the `bug_mg_uint8` fixture.
TEST_CASE("diff algorithms — index-type boundaries") {
    for (int n : {126, 127, 128, 32766, 32767, 32768}) {
        CAPTURE(n);
        auto a = seq(n);

        auto b_changed = a;
        b_changed[n / 2] = "CHANGED";
        check_all_algos(make_lines(a), make_lines(b_changed));

        auto b_appended = a;
        b_appended.push_back("EXTRA");
        check_all_algos(make_lines(a), make_lines(b_appended));
    }
}

// Characterization of the patience LIS concern (patience.hpp:95-100): the
// algorithm finds a suboptimal anchor set, but the Myers fallback guarantees a
// *valid* (and, since Myers is optimal, minimal) diff. So a prepend of one line
// still yields exactly one insert + all-common — correctness holds.
TEST_CASE("patience stays correct despite suboptimal anchoring") {
    std::vector<Line> A = lines("abcde");
    std::vector<Line> B = lines("xabcde");
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();

    REQUIRE(is_valid_transform(A, B, r));

    int common = 0, insert = 0, del = 0;
    for (const auto& e : r.edit_sequence) {
        common += e.type == EditType::Common;
        insert += e.type == EditType::Insert;
        del += e.type == EditType::Delete;
    }
    CHECK(common == 5);
    CHECK(insert == 1);
    CHECK(del == 0);
}

// Randomized reconstruct invariant: generated (a,b) drawn from a small alphabet —
// so common runs, inserts and deletes all occur — checked against every
// algorithm. A far wider input space than the fixed fixture corpus. The seed is
// fixed and CAPTURE(iter) logs the failing iteration, so any failure replays
// deterministically; it also runs under the ASAN build.
TEST_CASE("diff algorithms — randomized reconstruct invariant") {
    std::mt19937 rng(0xD1FFu);
    auto rand_seq = [&](int max_len, int alphabet) {
        std::uniform_int_distribution<int> len(0, max_len);
        std::uniform_int_distribution<int> sym(0, alphabet - 1);
        std::vector<std::string> v;
        const int n = len(rng);
        v.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; i++)
            v.push_back("L" + std::to_string(sym(rng)));
        return make_lines(v);
    };
    // Small alphabet + short sequences keep MyersGreedy's O(D*(N+M)) memory bounded
    // and make shared subsequences frequent (unique lines would be a trivial diff).
    for (int iter = 0; iter < 3000; iter++) {
        CAPTURE(iter);
        check_all_algos(rand_seq(40, 8), rand_seq(40, 8));
    }
}

#ifdef DIFFY_TEST_CASES_DIR
// Highest-ROI test: run every fixture pair through the algorithms and assert the
// reconstruct invariant. MyersGreedy is O(D*(N+M)) in memory, so it is capped to
// small pairs; MyersLinear and Patience run on all.
TEST_CASE("diff algorithms — fixture corpus reconstruct invariant") {
    namespace fs = std::filesystem;
    fs::path root{DIFFY_TEST_CASES_DIR};
    if (!fs::exists(root)) {
        WARN_MESSAGE(false, "fixture dir missing: " DIFFY_TEST_CASES_DIR);
        return;
    }

    int pairs = 0;
    int greedy_skipped = 0;
    const size_t kGreedyLineCap = 3000;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file())
            continue;
        // Fixtures are pairs of files ending in 'a' / 'b' (both `foo_a`/`foo_b`
        // and `0a`/`0b` naming appear in the corpus).
        std::string path = entry.path().string();
        if (path.empty() || path.back() != 'a')
            continue;
        std::string path_b = path;
        path_b.back() = 'b';
        if (!fs::exists(path_b))
            continue;

        CAPTURE(path);
        auto a_lines = readlines(path, false);
        auto b_lines = readlines(path_b, false);

        const bool include_greedy = (a_lines.size() + b_lines.size()) <= kGreedyLineCap;
        if (!include_greedy)
            greedy_skipped++;
        check_all_algos(a_lines, b_lines, include_greedy);
        pairs++;
    }

    MESSAGE("corpus pairs checked: " << pairs << " (myers-greedy capped on " << greedy_skipped
                                     << " large pairs)");
    CHECK(pairs > 0);
}
#endif
