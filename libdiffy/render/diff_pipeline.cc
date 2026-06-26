#include "diff_pipeline.hpp"

#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "highlight/language.hpp"
#include "highlight/scope.hpp"
#include "highlight/syntax_highlighter.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/tokenizer.hpp"

#include <utility>

namespace diffy {

namespace {

bool
compute_edit_sequence(Algo algorithm, bool ignore_whitespace, DiffInput<Line>& input, DiffResult* result) {
    switch (algorithm) {
        case Algo::kMyersGreedy:
            *result = MyersGreedy<Line>(input).compute();
            break;
        case Algo::kMyersLinear:
            *result = MyersLinear<Line>(input).compute();
            break;
        case Algo::kPatience:
            *result = Patience<Line>(input).compute();
            break;
        case Algo::kInvalid:
        default:
            return false;
    }

    // Treat changes where both sides are empty-ish as unchanged (mirrors the CLI).
    if (ignore_whitespace) {
        for (auto& seq : result->edit_sequence) {
            if ((int) seq.a_index >= (int) input.A.size() ||
                (int) seq.b_index >= (int) input.B.size()) {
                continue;
            }
            if (is_empty(input.A[seq.a_index].line) && is_empty(input.B[seq.b_index].line)) {
                seq.type = EditType::Common;
            }
        }
    }
    return true;
}

}  // namespace

DiffComputation
compute_annotated_diff(const std::string& a_text,
                       const std::string& b_text,
                       const std::string& a_name,
                       const std::string& b_name,
                       const DiffPipelineOptions& options) {
    DiffComputation c;
    c.a_name = a_name;
    c.b_name = b_name;
    c.a_lines = readlines_from_string(a_text, options.ignore_line_endings);
    c.b_lines = readlines_from_string(b_text, options.ignore_line_endings);

    auto input = c.input();

    DiffResult result;
    if (!compute_edit_sequence(options.algorithm, options.ignore_whitespace, input, &result)) {
        c.status = DiffResultStatus::Failed;
        return c;
    }

    c.status = result.status;
    if (result.status != DiffResultStatus::OK && result.status != DiffResultStatus::NoChanges) {
        return c;
    }

    auto hunks = compose_hunks(result.edit_sequence, options.context_lines);
    c.hunks = annotate_hunks(input, hunks, options.granularity, options.ignore_whitespace);

    // Syntax highlighting: parse each full buffer once; the language is inferred
    // from the file name. Returns empty (no-op) for unknown/oversized/binary.
    if (options.syntax_highlight) {
        const Language lang_a = language_for_path(a_name);
        const Language lang_b = language_for_path(b_name);
        c.a_highlights = highlight_source(a_text, lang_a);
        c.b_highlights = highlight_source(b_text, lang_b);

        // Enclosing-definition label per hunk (git-style "@@ ... @@ funcname").
        // Prefer the new side; fall back to the old side for pure deletions.
        const auto a_outline = scope_outline(a_text, lang_a);
        const auto b_outline = scope_outline(b_text, lang_b);
        for (auto& h : c.hunks) {
            int64_t a_change = -1, b_change = -1;
            for (const auto& el : h.a_lines) {
                if (el.type == EditType::Delete) { a_change = el.line_index; break; }
            }
            for (const auto& el : h.b_lines) {
                if (el.type == EditType::Insert) { b_change = el.line_index; break; }
            }
            h.context =
                hunk_context(a_outline, b_outline, a_change, b_change, h.from_start, h.to_start);
        }
    }
    return c;
}

DiffViewModel
build_diff_view_from_text(const std::string& a_text,
                          const std::string& b_text,
                          const std::string& a_name,
                          const std::string& b_name,
                          const DiffPipelineOptions& pipeline_options,
                          const DiffLayoutOptions& layout_options,
                          DiffComputation* out_computation) {
    DiffComputation c =
        compute_annotated_diff(a_text, b_text, a_name, b_name, pipeline_options);
    auto input = c.input();
    DiffViewModel model =
        build_diff_view(input, c.hunks, layout_options, &c.a_highlights, &c.b_highlights);
    if (out_computation) {
        *out_computation = std::move(c);
    }
    return model;
}

}  // namespace diffy
