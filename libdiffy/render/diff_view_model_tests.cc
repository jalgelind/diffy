#include <doctest.h>

#include "highlight/language.hpp"
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

TEST_CASE("render model: context gap between distant hunks reveals incrementally") {
    // 40 lines with two far-apart changes: the two hunks (each with 3 lines of
    // context) leave a wide run of hidden common lines between them. That gap precedes
    // the second hunk, so its expander metadata rides the second hunk's @@ HunkHeader
    // (only the tail gap keeps a standalone ContextGap marker); expanding it reveals
    // context on demand.
    std::string a, b;
    for (int i = 1; i <= 40; ++i) {
        const std::string n = std::to_string(i);
        a += "line " + n + "\n";
        if (i == 5) {
            b += "LINE 5\n";
        } else if (i == 35) {
            b += "LINE 35\n";
        } else {
            b += "line " + n + "\n";
        }
    }

    DiffPipelineOptions p = default_pipeline();
    p.syntax_highlight = false;
    DiffLayoutOptions layout;
    layout.mode = ViewMode::SideBySide;

    DiffComputation c = compute_annotated_diff(a, b, "a", "b", p);
    REQUIRE(c.hunks.size() == 2);  // two separated changes -> two hunks

    auto input = c.input();
    auto base = build_diff_view(input, c.hunks, layout, &c.a_highlights, &c.b_highlights);

    // The wide common region between the two hunks is carried as gap metadata on the
    // second hunk's @@ header (non-tail gaps ride their following HunkHeader); the tail
    // gap would be a ContextGap marker. Either kind can host a gap, so scan both.
    int gap_id = -1;
    int64_t hidden = 0;
    for (const auto& r : base.rows) {
        if ((r.kind == RowKind::HunkHeader || r.kind == RowKind::ContextGap) &&
            r.gap_id >= 0 && r.gap_hidden > hidden) {
            hidden = r.gap_hidden;
            gap_id = r.gap_id;
        }
    }
    REQUIRE(gap_id >= 0);
    CHECK(hidden > 10);  // plenty of lines hidden between the hunks

    const int base_content = count_content_rows(base);

    // Reveal 10 lines at the top of that gap: 10 more Content rows appear and the
    // marker's hidden count drops by 10.
    std::map<int, GapExpansion> exp;
    exp[gap_id].top = 10;
    auto grown = build_diff_view(input, c.hunks, layout, &c.a_highlights, &c.b_highlights, &exp);

    CHECK(count_content_rows(grown) == base_content + 10);
    int64_t hidden_after = -1;
    for (const auto& r : grown.rows) {
        if ((r.kind == RowKind::HunkHeader || r.kind == RowKind::ContextGap) &&
            r.gap_id == gap_id) {
            hidden_after = r.gap_hidden;
        }
    }
    CHECK(hidden_after == hidden - 10);
}

TEST_CASE("pipeline: force_language overrides filename-based detection") {
    if (!highlighting_available()) {
        return;  // built without tree-sitter: nothing to force
    }
    const std::string a = "int main() {\n    return 0;\n}\n";
    const std::string b = "int main() {\n    return 1;\n}\n";

    DiffPipelineOptions p = default_pipeline();
    p.syntax_highlight = true;

    // The names carry no extension, so detection finds no grammar.
    auto plain = compute_annotated_diff(a, b, "old", "new", p);
    CHECK(plain.a_highlights.empty());

    // Forcing one (extension-token form, as --language accepts) highlights both sides.
    p.force_language = "cpp";
    auto forced = compute_annotated_diff(a, b, "old", "new", p);
    CHECK(!forced.a_highlights.empty());
    CHECK(!forced.b_highlights.empty());
}

TEST_CASE("detect_cross_file_moves: a block relocated to another file is tagged") {
    // Build two files' diffs by hand: file A deletes a 3-line block; file B inserts
    // the identical block. Cross-file detection must pair them.
    auto content_row = [](int64_t oldno, int64_t newno, const std::string& text) {
        DiffRow r;
        r.kind = RowKind::Content;
        StyledSpan sp;
        sp.text = text;
        if (oldno >= 0) {
            r.old_lineno = oldno;
            r.left.present = true;
            r.left.spans = {sp};
        }
        if (newno >= 0) {
            r.new_lineno = newno;
            r.right.present = true;
            r.right.spans = {sp};
        }
        return r;
    };

    DiffViewModel a, b;
    for (int i = 0; i < 3; ++i) {
        a.rows.push_back(content_row(i + 1, -1, "block line " + std::to_string(i)));  // deleted from A
    }
    for (int i = 0; i < 3; ++i) {
        b.rows.push_back(content_row(-1, i + 20, "block line " + std::to_string(i)));  // inserted in B
    }

    detect_cross_file_moves({{"a.txt", &a}, {"b.txt", &b}});

    for (const auto& r : a.rows) {
        CHECK(r.move_id != 0);
        CHECK(r.move_file == "b.txt");  // points at where it moved to
        CHECK(r.move_line == 20);       // the insert block's first line
    }
    for (const auto& r : b.rows) {
        CHECK(r.move_id != 0);
        CHECK(r.move_file == "a.txt");  // points back at where it came from
        CHECK(r.move_line == 1);
    }
    // Both ends share the same move id.
    CHECK(a.rows.front().move_id == b.rows.front().move_id);
}

TEST_CASE("detect_cross_file_moves: a same-file block is left for the per-file pass") {
    // One file, delete+insert of the same block within it — cross-file must NOT tag it
    // (same file is skipped; detect_moves handles within-file).
    auto row = [](int64_t oldno, int64_t newno, const std::string& t) {
        DiffRow r;
        r.kind = RowKind::Content;
        StyledSpan sp;
        sp.text = t;
        if (oldno >= 0) {
            r.old_lineno = oldno;
            r.left.present = true;
            r.left.spans = {sp};
        }
        if (newno >= 0) {
            r.new_lineno = newno;
            r.right.present = true;
            r.right.spans = {sp};
        }
        return r;
    };
    DiffViewModel m;
    for (int i = 0; i < 3; ++i) {
        m.rows.push_back(row(i + 1, -1, "x" + std::to_string(i)));
    }
    for (int i = 0; i < 3; ++i) {
        m.rows.push_back(row(-1, i + 9, "x" + std::to_string(i)));
    }
    detect_cross_file_moves({{"same.txt", &m}});
    for (const auto& r : m.rows) {
        CHECK(r.move_id == 0);  // never cross-file tagged within one file
    }
}
