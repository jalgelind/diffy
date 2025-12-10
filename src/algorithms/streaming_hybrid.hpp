#pragma once

#include "algorithm.hpp"
#include "myers_linear.hpp"
#include "myers_greedy.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace diffy {

template <typename Unit>
class StreamingHybrid : public Algorithm<Unit> {
public:
    struct Anchor {
        int64_t a_start;
        int64_t b_start;
        int64_t length;
        double confidence;
        
        bool overlaps_with(const Anchor& other) const {
            bool a_overlap = (a_start < other.a_start + other.length) && 
                           (other.a_start < a_start + length);
            bool b_overlap = (b_start < other.b_start + other.length) && 
                           (other.b_start < b_start + length);
            return a_overlap && b_overlap;
        }
    };

    struct Region {
        int64_t a_start, a_end;
        int64_t b_start, b_end;
        bool is_anchor;
        size_t region_index;
    };

private:
    const gsl::span<Unit>& A;
    const gsl::span<Unit>& B;
    int64_t N;
    int64_t M;
    
    static constexpr int64_t MIN_ANCHOR_SIZE = 3;
    static constexpr double MIN_CONFIDENCE = 0.6;

    // Simple polynomial rolling hash
    static uint64_t hash_block(const gsl::span<Unit>& data, int64_t start, int64_t len) {
        const uint64_t PRIME = 31;
        const uint64_t MOD = 1000000009;
        
        uint64_t hash_value = 0;
        uint64_t power = 1;
        
        int64_t end = std::min(start + len, static_cast<int64_t>(data.size()));
        for (int64_t i = start; i < end; ++i) {
            // Use the Unit's hash() method if available (for Line types)
            uint64_t unit_hash = static_cast<uint64_t>(data[static_cast<size_t>(i)].hash());
            hash_value = (hash_value + (unit_hash % MOD) * power) % MOD;
            power = (power * PRIME) % MOD;
        }
        
        return hash_value;
    }

    int64_t extend_match(int64_t a_pos, int64_t b_pos) const {
        int64_t length = 0;
        while (a_pos + length < N && 
               b_pos + length < M && 
               A[static_cast<size_t>(a_pos + length)] == B[static_cast<size_t>(b_pos + length)]) {
            ++length;
        }
        return length;
    }

    double compute_confidence(int64_t length, int64_t a_pos, int64_t b_pos) const {
        double length_score = std::min(1.0, static_cast<double>(length) / 10.0);
        
        int64_t diagonal_dist = std::abs(a_pos - b_pos);
        double locality_score = 1.0 / (1.0 + static_cast<double>(diagonal_dist) / 100.0);
        
        return 0.7 * length_score + 0.3 * locality_score;
    }

    std::vector<Anchor> filter_overlapping_anchors(std::vector<Anchor> anchors) {
        if (anchors.empty()) return {};
        
        std::sort(anchors.begin(), anchors.end(), 
                  [](const Anchor& a, const Anchor& b) {
                      return a.confidence > b.confidence;
                  });
        
        std::vector<Anchor> filtered;
        
        for (const auto& anchor : anchors) {
            bool overlaps = false;
            
            for (const auto& existing : filtered) {
                if (anchor.overlaps_with(existing)) {
                    overlaps = true;
                    break;
                }
            }
            
            if (!overlaps) {
                filtered.push_back(anchor);
            }
        }
        
        std::sort(filtered.begin(), filtered.end(),
                  [](const Anchor& a, const Anchor& b) {
                      return a.a_start < b.a_start;
                  });
        
        return filtered;
    }

public:
    StreamingHybrid(DiffInput<Unit>& diff_input)
        : Algorithm<Unit>(diff_input)
        , A(diff_input.A)
        , B(diff_input.B)
        , N(static_cast<int64_t>(diff_input.A.size()))
        , M(static_cast<int64_t>(diff_input.B.size()))
    {}

    std::vector<Anchor> find_anchors() {
        if (N < MIN_ANCHOR_SIZE || M < MIN_ANCHOR_SIZE) {
            return {};
        }

        std::unordered_map<uint64_t, std::vector<int64_t>> hash_to_positions;
        
        for (int64_t i = 0; i <= N - MIN_ANCHOR_SIZE; ++i) {
            uint64_t h = hash_block(A, i, MIN_ANCHOR_SIZE);
            hash_to_positions[h].push_back(i);
        }

        std::vector<Anchor> candidates;
        
        for (int64_t j = 0; j <= M - MIN_ANCHOR_SIZE; ++j) {
            uint64_t h = hash_block(B, j, MIN_ANCHOR_SIZE);
            
            if (hash_to_positions.find(h) == hash_to_positions.end()) {
                continue;
            }
            
            for (int64_t i : hash_to_positions[h]) {
                bool match = true;
                for (int64_t k = 0; k < MIN_ANCHOR_SIZE; ++k) {
                    if (A[static_cast<size_t>(i + k)] != B[static_cast<size_t>(j + k)]) {
                        match = false;
                        break;
                    }
                }
                
                if (!match) continue;
                
                int64_t length = extend_match(i, j);
                
                if (length >= MIN_ANCHOR_SIZE) {
                    double conf = compute_confidence(length, i, j);
                    
                    if (conf >= MIN_CONFIDENCE) {
                        candidates.push_back({i, j, length, conf});
                    }
                }
            }
        }

        return filter_overlapping_anchors(candidates);
    }

    std::vector<Region> partition_into_regions(const std::vector<Anchor>& anchors) {
        std::vector<Region> regions;
        
        int64_t curr_a = 0;
        int64_t curr_b = 0;
        size_t region_idx = 0;
        
        for (const auto& anchor : anchors) {
            if (curr_a < anchor.a_start || curr_b < anchor.b_start) {
                regions.push_back({
                    curr_a, anchor.a_start,
                    curr_b, anchor.b_start,
                    false,
                    region_idx++
                });
            }
            
            regions.push_back({
                anchor.a_start, anchor.a_start + anchor.length,
                anchor.b_start, anchor.b_start + anchor.length,
                true,
                region_idx++
            });
            
            curr_a = anchor.a_start + anchor.length;
            curr_b = anchor.b_start + anchor.length;
        }
        
        if (curr_a < N || curr_b < M) {
            regions.push_back({
                curr_a, N,
                curr_b, M,
                false,
                region_idx++
            });
        }
        
        return regions;
    }

    DiffResult diff() override {
        // Try the streaming approach first
        auto anchors = find_anchors();
        auto regions = partition_into_regions(anchors);
        
        DiffResult result;
        result.status = DiffResultStatus::OK;
        bool streaming_failed = false;
        
        // Process regions sequentially for now
        // Future enhancement: parallel processing with thread pool
        for (const auto& region : regions) {
            if (region.is_anchor) {
                // Anchor: emit common edits directly
                for (int64_t i = 0; i < region.a_end - region.a_start; ++i) {
                    result.edit_sequence.push_back({
                        EditType::Common,
                        EditIndex(region.a_start + i),
                        EditIndex(region.b_start + i)
                    });
                }
            } else {
                // Diff region: run Myers
                int64_t region_a_size = region.a_end - region.a_start;
                int64_t region_b_size = region.b_end - region.b_start;
                
                // Validate region bounds
                if (region.a_start < 0 || region.a_end > N || region.a_start > region.a_end ||
                    region.b_start < 0 || region.b_end > M || region.b_start > region.b_end) {
                    streaming_failed = true;
                    break;
                }
                
                // Handle empty regions explicitly
                if (region_a_size == 0 && region_b_size == 0) {
                    continue; // Both empty, nothing to do
                } else if (region_a_size == 0 && region_b_size > 0) {
                    // Pure insertions
                    for (int64_t i = 0; i < region_b_size; ++i) {
                        result.edit_sequence.push_back({
                            EditType::Insert,
                            EditIndexInvalid,
                            EditIndex(region.b_start + i)
                        });
                    }
                    continue;
                } else if (region_b_size == 0 && region_a_size > 0) {
                    // Pure deletions
                    for (int64_t i = 0; i < region_a_size; ++i) {
                        result.edit_sequence.push_back({
                            EditType::Delete,
                            EditIndex(region.a_start + i),
                            EditIndexInvalid
                        });
                    }
                    continue;
                }
                
                auto a_slice = A.subspan(
                    static_cast<size_t>(region.a_start),
                    static_cast<size_t>(region_a_size)
                );
                auto b_slice = B.subspan(
                    static_cast<size_t>(region.b_start),
                    static_cast<size_t>(region_b_size)
                );
                
                DiffInput<Unit> region_input{a_slice, b_slice, "", ""};
                
                // Try MyersLinear first (more efficient)
                MyersLinear<Unit> region_algo(region_input);
                auto region_result = region_algo.compute();
                
                // If MyersLinear fails, fall back to MyersGreedy (more robust)
                if (region_result.status != DiffResultStatus::OK && 
                    region_result.status != DiffResultStatus::NoChanges) {
                    MyersGreedy<Unit> greedy_algo(region_input);
                    region_result = greedy_algo.compute();
                    
                    // If even greedy fails and region is large, split it
                    if (region_result.status != DiffResultStatus::OK && 
                        region_result.status != DiffResultStatus::NoChanges) {
                        
                        // For large regions, split in half and process recursively
                        if (region_a_size > 1000 || region_b_size > 1000) {
                            int64_t mid_a = region.a_start + region_a_size / 2;
                            int64_t mid_b = region.b_start + region_b_size / 2;
                            
                            // Create two sub-regions
                            std::vector<Region> sub_regions = {
                                {region.a_start, mid_a, region.b_start, mid_b, false, 0},
                                {mid_a, region.a_end, mid_b, region.b_end, false, 1}
                            };
                            
                            // Process each sub-region recursively
                            for (const auto& sub_region : sub_regions) {
                                int64_t sub_a_size = sub_region.a_end - sub_region.a_start;
                                int64_t sub_b_size = sub_region.b_end - sub_region.b_start;
                                
                                if (sub_a_size == 0 && sub_b_size == 0) {
                                    continue;
                                } else if (sub_a_size == 0) {
                                    for (int64_t i = 0; i < sub_b_size; ++i) {
                                        result.edit_sequence.push_back({
                                            EditType::Insert,
                                            EditIndexInvalid,
                                            EditIndex(sub_region.b_start + i)
                                        });
                                    }
                                } else if (sub_b_size == 0) {
                                    for (int64_t i = 0; i < sub_a_size; ++i) {
                                        result.edit_sequence.push_back({
                                            EditType::Delete,
                                            EditIndex(sub_region.a_start + i),
                                            EditIndexInvalid
                                        });
                                    }
                                } else {
                                    auto sub_a_slice = A.subspan(
                                        static_cast<size_t>(sub_region.a_start),
                                        static_cast<size_t>(sub_a_size)
                                    );
                                    auto sub_b_slice = B.subspan(
                                        static_cast<size_t>(sub_region.b_start),
                                        static_cast<size_t>(sub_b_size)
                                    );
                                    
                                    DiffInput<Unit> sub_input{sub_a_slice, sub_b_slice, "", ""};
                                    MyersGreedy<Unit> sub_algo(sub_input);
                                    auto sub_result = sub_algo.compute();
                                    
                                    if (sub_result.status != DiffResultStatus::OK &&
                                        sub_result.status != DiffResultStatus::NoChanges) {
                                        streaming_failed = true;
                                        break;
                                    }
                                    
                                    for (auto& edit : sub_result.edit_sequence) {
                                        if (edit.a_index.valid) {
                                            edit.a_index.value += sub_region.a_start;
                                        }
                                        if (edit.b_index.valid) {
                                            edit.b_index.value += sub_region.b_start;
                                        }
                                        result.edit_sequence.push_back(edit);
                                    }
                                }
                            }
                            continue; // Skip the normal edit adjustment below
                        }
                        
                        // If not large enough to split, mark streaming as failed
                        streaming_failed = true;
                        break;
                    }
                }
                
                // Adjust indices back to global coordinates
                for (auto& edit : region_result.edit_sequence) {
                    if (edit.a_index.valid) {
                        edit.a_index.value += region.a_start;
                    }
                    if (edit.b_index.valid) {
                        edit.b_index.value += region.b_start;
                    }
                    result.edit_sequence.push_back(edit);
                }
            }
            
            if (streaming_failed) {
                break;
            }
        }
        
        // If streaming approach failed, fall back to direct MyersGreedy on full input
        if (streaming_failed) {
            MyersGreedy<Unit> greedy(this->diff_input_);
            return greedy.compute();
        }
        
        return result;
    }
    
    // Get regions for external processing (enables parallel streaming)
    std::vector<Region> get_regions() {
        auto anchors = find_anchors();
        return partition_into_regions(anchors);
    }
};

}  // namespace diffy
