#pragma once

/*
    High-level diff facade: two text buffers in, annotated hunks / a render model
    out. This is the single entry point the CLI and any embedding frontend use so
    neither re-wires the readlines -> compute -> compose -> annotate stages by hand.
*/

#include "algorithms/algorithm.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "render/diff_view_model.hpp"
#include "util/readlines.hpp"

#include <gsl/span>
#include <string>
#include <vector>

namespace diffy {

// Owns the per-side line storage so the DiffInput spans it hands out stay valid.
struct DiffComputation {
    std::vector<Line> a_lines;
    std::vector<Line> b_lines;
    std::string a_name;
    std::string b_name;
    std::vector<AnnotatedHunk> hunks;
    DiffResultStatus status = DiffResultStatus::Failed;
    LineHighlights a_highlights;  // per-line syntax runs for the old side (may be empty)
    LineHighlights b_highlights;  // per-line syntax runs for the new side (may be empty)

    DiffInput<Line>
    input() {
        return DiffInput<Line>{gsl::span<Line>(a_lines), gsl::span<Line>(b_lines), a_name, b_name};
    }
};

// Run the whole pipeline on two in-memory buffers.
DiffComputation
compute_annotated_diff(const std::string& a_text,
                       const std::string& b_text,
                       const std::string& a_name,
                       const std::string& b_name,
                       const DiffPipelineOptions& options);

// Convenience: pipeline + build_diff_view in one call. The returned model owns
// its own text, so it outlives the (optionally returned) DiffComputation.
DiffViewModel
build_diff_view_from_text(const std::string& a_text,
                          const std::string& b_text,
                          const std::string& a_name,
                          const std::string& b_name,
                          const DiffPipelineOptions& pipeline_options,
                          const DiffLayoutOptions& layout_options,
                          DiffComputation* out_computation = nullptr);

}  // namespace diffy
