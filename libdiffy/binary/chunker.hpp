#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace diffy {

// One content-defined chunk of a file. Matched purely by (checksum, length),
// mirroring how Line is matched by checksum — so the existing line-diff
// algorithms (Myers/patience) align a sequence of Chunks unchanged.
struct Chunk {
    uint64_t offset = 0;    // byte offset of the chunk within its file
    uint32_t length = 0;    // chunk length in bytes
    uint32_t checksum = 0;  // crc32c over the chunk bytes

    uint32_t
    hash() const {
        return checksum;
    }

    // Length is part of equality so same-checksum chunks of different length
    // never compare equal. A residual checksum collision (same checksum AND
    // length, different bytes) is caught downstream by a memcmp guard.
    bool
    operator==(const Chunk& o) const {
        return checksum == o.checksum && length == o.length;
    }

    // Total, deterministic order (patience sorts matches).
    bool
    operator<(const Chunk& o) const {
        return checksum != o.checksum ? checksum < o.checksum : length < o.length;
    }
};

struct ChunkParams {
    uint32_t min_size = 2048;    // never cut before this many bytes
    uint32_t max_size = 65536;   // always cut at this many bytes
    uint32_t mask_bits = 12;     // avg extra bytes past min ~= 2^mask_bits
};

// Split `data` into content-defined chunks using a Gear rolling hash. Boundaries
// are content-relative, so a local edit perturbs only the chunk(s) around it and
// the rest of the file re-synchronises (the anti-cascade property). Deterministic:
// the same bytes always produce the same cuts.
std::vector<Chunk>
chunk_bytes(const uint8_t* data, size_t len, const ChunkParams& params = {});

}  // namespace diffy
