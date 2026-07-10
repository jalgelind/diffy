// Tests for the indent/slider heuristic (ALG-2), exercised through the pipeline.

#include "render/diff_pipeline.hpp"

#include <doctest.h>

#include <string>
#include <vector>

using namespace diffy;

TEST_CASE("indent heuristic: an added function slides to a clean block boundary") {
    DiffPipelineOptions opt;  // defaults: patience, no ignore-whitespace
    auto c = compute_annotated_diff("func foo() {\n}\n",
                                    "func foo() {\n}\n\nfunc bar() {\n}\n", "a", "b", opt);
    REQUIRE(c.status == DiffResultStatus::OK);

    std::vector<std::string> inserted;
    for (const auto& h : c.hunks) {
        for (const auto& el : h.b_lines) {
            if (el.type == EditType::Insert) {
                inserted.push_back(c.b_lines[static_cast<size_t>(el.line_index.value)].line);
            }
        }
    }
    // The inserted run must be the natural block [blank, "func bar() {", "}"], not a
    // mid-block split like ["}", blank, "func bar() {"] that a raw LCS can produce.
    REQUIRE(inserted.size() == 3);
    CHECK(inserted[0] == "\n");
    CHECK(inserted[1] == "func bar() {\n");
    CHECK(inserted[2] == "}\n");
}

TEST_CASE("indent heuristic: reconstruct invariant (both sides rebuild exactly)") {
    // Whatever the heuristic does, the diff must still reconstruct A and B: applying
    // the Common+Delete edits yields A, the Common+Insert edits yield B.
    DiffPipelineOptions opt;
    const std::string a = "a\nb\nc\n\nd\ne\n";
    const std::string b = "a\n\nd\ne\nb\nc\n";  // b,c relocated + a blank shuffled
    auto c = compute_annotated_diff(a, b, "a", "b", opt);
    REQUIRE(c.status == DiffResultStatus::OK);

    std::string ra, rb;
    for (const auto& h : c.hunks) {
        for (const auto& el : h.a_lines) {
            if (el.type != EditType::Insert) {
                ra += c.a_lines[static_cast<size_t>(el.line_index.value)].line;
            }
        }
        for (const auto& el : h.b_lines) {
            if (el.type != EditType::Delete) {
                rb += c.b_lines[static_cast<size_t>(el.line_index.value)].line;
            }
        }
    }
    // (Only valid when the whole file is one hunk, which it is for this small input.)
    CHECK(ra == a);
    CHECK(rb == b);
}
