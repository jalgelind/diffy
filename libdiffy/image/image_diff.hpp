#pragma once

#include <cstdint>
#include <vector>

namespace diffy {

struct ImageDiffOptions {
    // Matching threshold, 0..1 — larger tolerates bigger per-pixel differences.
    double threshold = 0.1;
    // Count anti-aliased pixels as differences too (default: ignore AA fringes).
    bool include_aa = false;
    // Produce the overlay bitmap (GUI wants it; CLI similarity-only can skip it).
    bool compute_overlay = true;
};

struct ImageDiffResult {
    bool comparable = false;  // false when dimensions differ / inputs unusable
    int width = 0;
    int height = 0;
    uint64_t total_px = 0;
    uint64_t changed_px = 0;
    double similarity = 0.0;  // 1 - changed/total, in [0,1]
    // width*height*4 RGBA: changed pixels magenta over a dimmed grayscale base.
    // Empty when !comparable or compute_overlay is false.
    std::vector<uint8_t> overlay_rgba;
};

// Per-pixel perceptual image diff (a C++ port of the pixelmatch algorithm:
// YIQ colour distance + anti-alias detection). Both buffers must be tightly
// packed RGBA8 of size width*height*4; mismatched dimensions => comparable=false.
ImageDiffResult
image_diff(const std::vector<uint8_t>& a_rgba,
           const std::vector<uint8_t>& b_rgba,
           int width,
           int height,
           const ImageDiffOptions& opts = {});

}  // namespace diffy
