// Tests for annotate_hunks: splitting hunk lines into typed segments.

#include "algorithms/patience.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/hash.hpp"
#include "util/readlines.hpp"

#include <doctest.h>

#include <algorithm>
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

TEST_CASE("annotate_hunks — token granularity pairs similar lines (no cross-line scatter)") {
    // Two changed lines share almost all their tokens; ALG-3 must pair each deleted
    // line with its similar inserted line and diff within the pair, so only that
    // line's own differing argument is highlighted — not a token borrowed from the
    // other changed line (the old whole-hunk "token soup" failure).
    auto A = mk({"ctx", "int alpha = compute(x);", "int beta = compute(y);", "ctx2"});
    auto B = mk({"ctx", "int alpha = compute(z);", "int beta = compute(w);", "ctx2"});
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    REQUIRE(hunks.size() == 1);

    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);
    REQUIRE(annotated.size() == 1);
    check_tiling(A, annotated[0].a_lines);
    check_tiling(B, annotated[0].b_lines);

    auto changed_texts = [](const std::string& src, const EditLine& el, EditType want) {
        std::vector<std::string> out;
        for (const auto& seg : el.segments) {
            if (seg.type == want) {
                out.push_back(src.substr(seg.start, seg.length));
            }
        }
        return out;
    };
    auto has = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };

    for (const auto& el : annotated[0].b_lines) {
        if (el.type != EditType::Insert) {
            continue;
        }
        const std::string& src = B[el.line_index.value].line;
        auto ch = changed_texts(src, el, EditType::Insert);
        // The shared identifiers must stay Common regardless of tokenization.
        CHECK(!has(ch, "alpha"));
        CHECK(!has(ch, "beta"));
        CHECK(!has(ch, "compute"));
        if (src.find("alpha") != std::string::npos) {
            CHECK(has(ch, "z"));   // this line's own new arg
            CHECK(!has(ch, "w"));  // never the other line's
        } else {
            CHECK(has(ch, "w"));
            CHECK(!has(ch, "z"));
        }
    }
}

TEST_CASE("annotate_hunks — detects a moved block (GAP-9)") {
    // Two blocks swap order; the longer (x1..x4) stays the common anchor, so the
    // shorter 3-line block (y1..y3) is what the diff sees deleted then re-inserted
    // — exactly the moved-function case, and >= the 3-line move threshold.
    auto A = mk({"x1", "x2", "x3", "x4", "y1", "y2", "y3"});
    auto B = mk({"y1", "y2", "y3", "x1", "x2", "x3", "x4"});
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);

    int del_move = 0, ins_move = 0;
    int64_t del_ptr = 0, ins_ptr = 0;
    for (const auto& h : annotated) {
        for (const auto& el : h.a_lines) {
            if (el.type == EditType::Delete && el.move_id != 0) {
                del_move = el.move_id;
                del_ptr = el.move_line;
            }
        }
        for (const auto& el : h.b_lines) {
            if (el.type == EditType::Insert && el.move_id != 0) {
                ins_move = el.move_id;
                ins_ptr = el.move_line;
            }
        }
    }
    CHECK(del_move != 0);              // the deleted block is flagged moved
    CHECK(del_move == ins_move);       // both ends share the move id
    CHECK(del_ptr > 0);                // deletion points at where it moved to
    CHECK(ins_ptr > 0);                // insertion points back at where it came from
}

TEST_CASE("annotate_hunks — a bracket-only block is not flagged as a move (GAP-9)") {
    // Same swap shape as the moved-block test, but the relocated run is pure closing
    // brackets — a coincidental line-hash match that relocates nothing. It must NOT be
    // tagged as a move, however long the run (the content gate has no substantive lines).
    auto A = mk({"anchor line one", "anchor line two", "anchor line three",
                 "anchor line four", "}", "})", "});"});
    auto B = mk({"}", "})", "});", "anchor line one", "anchor line two",
                 "anchor line three", "anchor line four"});
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);
    for (const auto& h : annotated) {
        for (const auto& el : h.a_lines) {
            CHECK(el.move_id == 0);
        }
        for (const auto& el : h.b_lines) {
            CHECK(el.move_id == 0);
        }
    }
}

TEST_CASE("annotate_hunks — a plain edit is not a move") {
    auto A = mk({"ctx", "old value here", "ctx2"});
    auto B = mk({"ctx", "new value here", "ctx2"});
    DiffInput<Line> in{gsl::span<Line>{A}, gsl::span<Line>{B}, "a", "b"};
    auto r = Patience<Line>(in).compute();
    auto hunks = compose_hunks(r.edit_sequence, 3);
    auto annotated = annotate_hunks(in, hunks, EditGranularity::Token, false);
    for (const auto& h : annotated) {
        for (const auto& el : h.a_lines) {
            CHECK(el.move_id == 0);
        }
        for (const auto& el : h.b_lines) {
            CHECK(el.move_id == 0);
        }
    }
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
