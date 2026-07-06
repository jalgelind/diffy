#pragma once

#include <cstdint>
#include <vector>

#include <gsl/span>

namespace diffy {

// An image decoded to tightly-packed RGBA8 (width*height*4 bytes).
struct DecodedImage {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Decode png/jpg/gif/bmp/… (via stb_image) to RGBA8. Returns ok=false on decode
// failure, empty input, or if either dimension exceeds `max_dim` (a guard so a
// hostile/huge header can't make us allocate gigabytes).
DecodedImage
decode_image(gsl::span<const uint8_t> data, int max_dim = 8192);

}  // namespace diffy
