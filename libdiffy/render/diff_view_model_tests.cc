#include <doctest.h>

#include "render/diff_pipeline.hpp"
#include "render/diff_view_model.hpp"

using namespace diffy;

namespace {

DiffPipelineOptions
default_pipeline() {
    DiffPipelineOptions p;
    p.algorithm = Algo::kPatience;
    p.context_lines = 3;
    p.granularity = EditGranularity::Token;
    return p;
}

int
count_content_rows(const DiffViewModel& m) {
    int n = 0;
    for (const auto& r : m.rows) {
        if (r.kind == RowKind::Content) {
            ++n;
        }
    }
    return n;
}

bool
has_left_type(const DiffViewModel& m, EditType t) {
    for (const auto& r : m.rows) {
        if (r.kind == RowKind::Content && r.left.present && r.left.type == t) {
            return true;
        }
    }
    return false;
}

bool
has_right_type(const DiffViewModel& m, EditType t) {
    for (const auto& r : m.rows) {
        if (r.kind == RowKind::Content && r.right.present && r.right.type == t) {
            return true;
        }
    }
    return false;
}

bool
any_span_style(const DiffViewModel& m, SpanStyle s) {
    for (const auto& r : m.rows) {
        for (const auto* cell : {&r.left, &r.right}) {
            for (const auto& span : cell->spans) {
                if (span.style == s) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE("render model: identical input yields no hunks") {
    const std::string a = "alpha\nbeta\ngamma\n";
    DiffLayoutOptions layout;
    layout.mode = ViewMode::SideBySide;
    auto m = build_diff_view_from_text(a, a, "a", "b", default_pipeline(), layout);
    CHECK(m.rows.empty());
}

TEST_CASE("render model: side-by-side pairs a changed line") {
    const std::string a = "alpha\nbeta\ngamma\n";
    const std::string b = "alpha\nBETA\ngamma\n";

    DiffLayoutOptions layout;
    layout.mode = ViewMode::SideBySide;
    auto m = build_diff_view_from_text(a, b, "a", "b", default_pipeline(), layout);

    REQUIRE(!m.rows.empty());
    CHECK(m.mode == ViewMode::SideBySide);
    CHECK(m.rows.front().kind == RowKind::HunkHeader);

    // The change shows as a delete on the left and an insert on the right.
    CHECK(has_left_type(m, EditType::Delete));
    CHECK(has_right_type(m, EditType::Insert));

    // Context lines survive on both sides.
    CHECK(has_left_type(m, EditType::Common));
    CHECK(has_right_type(m, EditType::Common));

    // Token-level highlight is present on both sides.
    CHECK(any_span_style(m, SpanStyle::DeleteToken));
    CHECK(any_span_style(m, SpanStyle::InsertToken));

    // Line numbers populated on a common (context) row.
    bool found_numbers = false;
    for (const auto& r : m.rows) {
        if (r.kind == RowKind::Content && r.old_lineno && r.new_lineno) {
            found_numbers = true;
            break;
        }
    }
    CHECK(found_numbers);
}

TEST_CASE("render model: unified emits delete then insert rows") {
    const std::string a = "alpha\nbeta\ngamma\n";
    const std::string b = "alpha\nBETA\ngamma\n";

    DiffLayoutOptions layout;
    layout.mode = ViewMode::Unified;
    auto m = build_diff_view_from_text(a, b, "a", "b", default_pipeline(), layout);

    REQUIRE(!m.rows.empty());
    CHECK(m.mode == ViewMode::Unified);

    // In unified mode every content row uses the left cell; right is unused.
    for (const auto& r : m.rows) {
        if (r.kind == RowKind::Content) {
            CHECK_FALSE(r.right.present);
        }
    }
    CHECK(has_left_type(m, EditType::Delete));
    CHECK(has_left_type(m, EditType::Insert));
    CHECK(has_left_type(m, EditType::Common));
}

TEST_CASE("render model: ignore-whitespace one-sided edit doesn't spawn phantom rows") {
    // Regression: a=["","x"], b=["",""] — delete "x", insert a blank line. Under
    // ignore_whitespace the blank Insert's *invalid* a_index still carried a leftover
    // value that pointed at the blank A[0]. The old guard compared that value against
    // the buffer size (in range), so is_empty(A[0]) && is_empty(inserted) held and the
    // one-sided edit was retyped to Common. That desynced the edit from its one-sided
    // index, inflating the hunk's from_count/to_count in compose_hunks and leaving
    // annotate_tokens a phantom default-constructed EditLine — an extra empty row that
    // repeated a line number. Now one-sided edits are skipped (guard on .valid), so no
    // phantom appears. (This only bit "sometimes" — when the garbage index happened to
    // land on a blank line.)
    const std::string a = "\nx\n";  // ["", "x"]
    const std::string b = "\n\n";   // ["", ""]

    DiffPipelineOptions p = default_pipeline();
    p.ignore_whitespace = true;

    for (auto mode : {ViewMode::Unified, ViewMode::SideBySide}) {
        DiffLayoutOptions layout;
        layout.mode = mode;
        auto m = build_diff_view_from_text(a, b, "a", "b", p, layout);

        // Old/new line numbers must be strictly increasing down the model; a phantom
        // row repeats an earlier number (its default index reads back as line 1).
        long prev_old = 0, prev_new = 0;
        for (const auto& r : m.rows) {
            if (r.kind != RowKind::Content) {
                continue;
            }
            if (r.left.present && r.old_lineno) {
                CHECK(*r.old_lineno > prev_old);
                CHECK(*r.old_lineno <= 2);  // a has 2 lines
                prev_old = *r.old_lineno;
            }
            if (r.right.present && r.new_lineno) {
                CHECK(*r.new_lineno > prev_new);
                CHECK(*r.new_lineno <= 2);  // b has 2 lines
                prev_new = *r.new_lineno;
            }
        }
    }
}

TEST_CASE("render model: pure insertion has only insert/common") {
    const std::string a = "one\ntwo\n";
    const std::string b = "one\ninserted\ntwo\n";

    DiffLayoutOptions layout;
    layout.mode = ViewMode::SideBySide;
    auto m = build_diff_view_from_text(a, b, "a", "b", default_pipeline(), layout);

    REQUIRE(!m.rows.empty());
    CHECK(has_right_type(m, EditType::Insert));
    CHECK(count_content_rows(m) >= 3);  // 2 context + 1 inserted (alignment may pad)
}
