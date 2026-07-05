// Tests for annotate_hunks: splitting hunk lines into typed segments.

#include "algorithms/patience.hpp"
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

// Each EditLine's segments must tile its source line contiguously and fully.
void
check_tiling(const std::vector<Line>& content, const std::vector<EditLine>& lines) {
    for (const auto& el : lines) {
        REQUIRE(el.line_index.valid);
        REQUIRE(el.line_index.value < static_cast<int64_t>(content.size()));
        const std::string& s = content[el.line_index.value].line;
        std::size_t pos = 0;
        for (const auto& seg : el.segments) {
            CHECK(seg.start == pos);
            pos += seg.length;
        }
        CHECK(pos == s.size());
    }
}

}  // namespace

TEST_CASE("annotate_hunks — line granularity") {
    auto A = mk({"common header", "old middle", "common footer"});
    auto B = mk({"common header", "new middle", "common footer"});
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    REQUIRE(hunks.size() == 1);

    auto annotated = annotate_hunks(in, hunks, EditGranularity::Line, false);
    REQUIRE(annotated.size() == 1);
    const auto& h = annotated[0];

    // One EditLine per a-bearing / b-bearing edit.
    CHECK(static_cast<int64_t>(h.a_lines.size()) == hunks[0].from_count);
    CHECK(static_cast<int64_t>(h.b_lines.size()) == hunks[0].to_count);

    check_tiling(A, h.a_lines);
    check_tiling(B, h.b_lines);

    int deletes = 0, common_a = 0, inserts = 0, common_b = 0;
    for (const auto& el : h.a_lines) {
        deletes += el.type == EditType::Delete;
        common_a += el.type == EditType::Common;
    }
    for (const auto& el : h.b_lines) {
        inserts += el.type == EditType::Insert;
        common_b += el.type == EditType::Common;
    }
    CHECK(deletes == 1);
    CHECK(common_a == 2);
    CHECK(inserts == 1);
    CHECK(common_b == 2);
}

TEST_CASE("annotate_hunks — token granularity keeps shared tokens common") {
    auto A = mk({"ctx", "the old value", "ctx2"});
    auto B = mk({"ctx", "the new value", "ctx2"});
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    REQUIRE(hunks.size() == 1);

    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);
    REQUIRE(annotated.size() == 1);

    check_tiling(A, annotated[0].a_lines);
    check_tiling(B, annotated[0].b_lines);

    // On the changed (Insert) line, the shared words "the"/"value" stay Common.
    bool found_common_segment = false;
    for (const auto& el : annotated[0].b_lines) {
        if (el.type == EditType::Insert) {
            for (const auto& seg : el.segments)
                if (seg.type == EditType::Common)
                    found_common_segment = true;
        }
    }
    CHECK(found_common_segment);
}

TEST_CASE("annotate_hunks — ignore_whitespace marks whitespace segments common") {
    auto A = mk({"ctx", "value", "ctx2"});
    auto B = mk({"ctx", "value   ", "ctx2"});  // trailing whitespace added
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    REQUIRE(hunks.size() == 1);

    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, true);
    REQUIRE(annotated.size() == 1);

    bool saw_whitespace_segment = false;
    for (const auto& el : annotated[0].b_lines) {
        for (const auto& seg : el.segments) {
            if (seg.flags & (TokenFlagSpace | TokenFlagTab)) {
                saw_whitespace_segment = true;
                CHECK(seg.type == EditType::Common);
            }
        }
    }
    CHECK(saw_whitespace_segment);
}
