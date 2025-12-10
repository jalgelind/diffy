#pragma once

#include "algorithms/streaming_hybrid.hpp"
#include "algorithms/myers_linear.hpp"
#include "diff_hunk.hpp"
#include "util/thread_pool.hpp"
#include "util/ordered_task_queue.hpp"

#include <memory>

namespace diffy {

// Compose hunks with streaming from regions
// This enables progressive display as regions are processed
template <typename Unit>
HunkStream compose_hunks_streaming(
    const std::vector<typename StreamingHybrid<Unit>::Region>& regions,
    const gsl::span<Unit>& A,
    const gsl::span<Unit>& B,
    int64_t context_size
) {
    if (regions.empty()) {
        return {};
    }

    auto& pool = global_thread_pool();
    const std::size_t capacity = std::max<std::size_t>(1, pool.thread_count() * 2);
    auto queue = std::make_shared<OrderedTaskQueue<Hunk>>(capacity);
    
    const std::size_t total_regions = regions.size();
    
    // Shared pointers for thread safety
    auto a_ptr = std::make_shared<gsl::span<Unit>>(A);
    auto b_ptr = std::make_shared<gsl::span<Unit>>(B);
    
    for (const auto& region : regions) {
        const size_t idx = region.region_index;
        
        if (region.is_anchor) {
            // Anchor region - build hunk immediately (no Myers needed)
            Hunk hunk;
            hunk.from_start = region.a_start + 1;  // 1-indexed for unified diff
            hunk.from_count = region.a_end - region.a_start;
            hunk.to_start = region.b_start + 1;
            hunk.to_count = region.b_end - region.b_start;
            
            // Add common edits
            hunk.edit_units.reserve(static_cast<size_t>(hunk.from_count));
            for (int64_t i = 0; i < hunk.from_count; ++i) {
                hunk.edit_units.push_back({
                    EditType::Common,
                    EditIndex(region.a_start + i),
                    EditIndex(region.b_start + i)
                });
            }
            
            queue->push(idx, std::move(hunk));
            
        } else {
            // Diff region - compute in parallel using Myers
            pool.enqueue([queue, a_ptr, b_ptr, region, idx, context_size] {
                try {
                    const auto& A = *a_ptr;
                    const auto& B = *b_ptr;
                    
                    // Handle empty regions
                    if (region.a_start >= region.a_end && region.b_start >= region.b_end) {
                        Hunk empty_hunk;
                        empty_hunk.from_start = region.a_start + 1;
                        empty_hunk.to_start = region.b_start + 1;
                        empty_hunk.from_count = 0;
                        empty_hunk.to_count = 0;
                        queue->push(idx, std::move(empty_hunk));
                        return;
                    }
                    
                    // Create slices for this region
                    auto a_size = static_cast<size_t>(region.a_end - region.a_start);
                    auto b_size = static_cast<size_t>(region.b_end - region.b_start);
                    
                    auto a_slice = A.subspan(static_cast<size_t>(region.a_start), a_size);
                    auto b_slice = B.subspan(static_cast<size_t>(region.b_start), b_size);
                    
                    // Run Myers on this region
                    DiffInput<Unit> region_input{a_slice, b_slice, "", ""};
                    MyersLinear<Unit> algorithm(region_input);
                    auto region_result = algorithm.compute();
                    
                    if (region_result.status != DiffResultStatus::OK) {
                        throw std::runtime_error("Region diff failed");
                    }
                    
                    // Adjust indices to global coordinates
                    for (auto& edit : region_result.edit_sequence) {
                        if (edit.a_index.valid) {
                            edit.a_index.value += region.a_start;
                        }
                        if (edit.b_index.valid) {
                            edit.b_index.value += region.b_start;
                        }
                    }
                    
                    // Convert to hunk
                    Hunk hunk;
                    hunk.from_start = region.a_start + 1;
                    hunk.to_start = region.b_start + 1;
                    hunk.edit_units = std::move(region_result.edit_sequence);
                    
                    // Count edits
                    hunk.from_count = 0;
                    hunk.to_count = 0;
                    for (const auto& edit : hunk.edit_units) {
                        if (edit.type != EditType::Insert) hunk.from_count++;
                        if (edit.type != EditType::Delete) hunk.to_count++;
                    }
                    
                    queue->push(idx, std::move(hunk));
                    
                } catch (...) {
                    queue->push_exception(idx, std::current_exception());
                }
            });
        }
    }
    
    return {total_regions, queue};
}

// Streaming version of compose_hunks that works with StreamingHybrid
// Returns immediately with HunkStream that can be consumed progressively
template <typename Unit>
HunkStream compose_hunks_from_streaming(
    StreamingHybrid<Unit>& algorithm,
    int64_t context_size
) {
    // Find anchors and partition
    auto anchors = algorithm.find_anchors();
    auto regions = algorithm.partition_into_regions(anchors);
    
    // Get input spans
    auto& A = algorithm.diff_input_.A;
    auto& B = algorithm.diff_input_.B;
    
    // Return streaming hunk queue
    return compose_hunks_streaming<Unit>(regions, A, B, context_size);
}

}  // namespace diffy
