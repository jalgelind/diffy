#include "diff_pipeline.hpp"

#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
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
    DiffViewModel model = build_diff_view(input, c.hunks, layout_options);
    if (out_computation) {
        *out_computation = std::move(c);
    }
    return model;
}

}  // namespace diffy
