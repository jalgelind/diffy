#include "binary/chunker.hpp"

#include "util/hash.hpp"

namespace diffy {

namespace {

// Gear hash table: 256 pseudo-random 64-bit values, generated deterministically
// with splitmix64 from a fixed seed so cuts are reproducible across runs/builds.
struct GearTable {
    uint64_t g[256];
    GearTable() {
        uint64_t x = 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < 256; ++i) {
            x += 0x9E3779B97F4A7C15ull;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z = z ^ (z >> 31);
            g[i] = z;
        }
    }
};

const GearTable&
gear_table() {
    static const GearTable table;
    return table;
}

}  // namespace

std::vector<Chunk>
chunk_bytes(const uint8_t* data, size_t len, const ChunkParams& params) {
    std::vector<Chunk> chunks;
    if (len == 0) {
        return chunks;
    }

    const uint64_t* g = gear_table().g;
    const uint64_t mask = (params.mask_bits >= 64) ? ~0ull : ((1ull << params.mask_bits) - 1);
    // Test the HIGH bits of the rolling hash (FastCDC), not the low ones: with a
    // shift-add hash the low bits are dominated by the last handful of bytes, so
    // cuts cluster on low-entropy data. The high bits are better mixed and give
    // more uniform boundary placement. (Changes cut positions -> hex goldens.)
    const int mask_shift =
        (params.mask_bits == 0 || params.mask_bits >= 64) ? 0 : (64 - static_cast<int>(params.mask_bits));
    const uint64_t boundary_mask = mask << mask_shift;

    size_t i = 0;
    while (i < len) {
        const size_t start = i;
        const size_t max_end = (len - start > params.max_size) ? start + params.max_size : len;
        const size_t min_end = (len - start > params.min_size) ? start + params.min_size : len;

        uint64_t h = 0;
        size_t j = start;
        // Roll through the minimum span without cutting (keeps the hash primed).
        for (; j < min_end; ++j) {
            h = (h << 1) + g[data[j]];
        }
        // Past the minimum, cut on the first boundary or at the maximum.
        for (; j < max_end; ++j) {
            h = (h << 1) + g[data[j]];
            if ((h & boundary_mask) == 0) {
                ++j;
                break;
            }
        }

        const uint32_t length = static_cast<uint32_t>(j - start);
        const uint32_t checksum = hash::hash(data + start, length);
        chunks.push_back({static_cast<uint64_t>(start), length, checksum});
        i = j;
    }

    return chunks;
}

}  // namespace diffy
