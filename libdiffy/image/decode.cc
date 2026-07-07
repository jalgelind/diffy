#include "image/decode.hpp"

#include <climits>

// Single translation unit that instantiates stb_image. Decode from memory only.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

namespace diffy {

DecodedImage
decode_image(gsl::span<const uint8_t> data, int max_dim) {
    DecodedImage out;
    if (data.empty() || data.size() > static_cast<size_t>(INT_MAX)) {
        return out;
    }
    const int len = static_cast<int>(data.size());

    // Reject oversized images before allocating (stbi_info doesn't decode). If the
    // header can't be read at all, fail closed instead of falling through to an
    // unguarded stbi_load, which could allocate up to ~2 GB on hostile input.
    int w = 0, h = 0, ch = 0;
    if (!stbi_info_from_memory(data.data(), len, &w, &h, &ch)) {
        return out;
    }
    if (w <= 0 || h <= 0 || w > max_dim || h > max_dim) {
        return out;
    }

    int rw = 0, rh = 0, rch = 0;
    stbi_uc* px = stbi_load_from_memory(data.data(), len, &rw, &rh, &rch, 4);
    if (!px) {
        return out;
    }
    out.ok = true;
    out.width = rw;
    out.height = rh;
    out.rgba.assign(px, px + static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4);
    stbi_image_free(px);
    return out;
}

}  // namespace diffy
