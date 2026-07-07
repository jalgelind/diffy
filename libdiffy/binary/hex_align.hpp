#pragma once

#include "binary/chunker.hpp"

#include <cstdint>
#include <vector>

#include <gsl/span>

namespace diffy {

// A contiguous run in the aligned view of two binaries. Segments are emitted in
// order and together cover both files end to end.
enum class HexSegKind : uint8_t {
    Equal,    // a_len == b_len, bytes equal        (context)
    Replace,  // a_len == b_len, bytes differ       (aligned change, both sides)
    OnlyA,    // bytes present only in A (removed);  b_len == 0
    OnlyB,    // bytes present only in B (added);    a_len == 0
};

struct HexSegment {
    HexSegKind kind;
    uint64_t a_offset;
    uint64_t a_len;
    uint64_t b_offset;
    uint64_t b_len;
};

using HexAlignment = std::vector<HexSegment>;

struct HexAlignParams {
    // --hex-global: byte-align the whole files instead of chunking first. Only
    // honoured when both files fit the O(n*m) aligner's cell budget (derived from
    // byte_cap below — the same bound the per-region refiner uses); larger inputs
    // fall back to the chunked path so the DP table can't blow up.
    bool force_global = false;
    // Per-side byte cap for the O(n*m) byte aligner. Whole-file global alignment
    // and per-region refinement both stay under this; larger changed regions are
    // left coarse (whole-removed + whole-added) and flagged via `truncated`.
    uint32_t byte_cap = 4096;
    ChunkParams chunk;
};

// Produce the aligned view of two binaries. When `truncated` is non-null it is
// set to true if any changed region was too large to byte-refine (so the caller
// can tell the user the diff is coarse there).
HexAlignment
hex_align(gsl::span<const uint8_t> a,
          gsl::span<const uint8_t> b,
          const HexAlignParams& params = {},
          bool* truncated = nullptr);

}  // namespace diffy
