#pragma once

#include <cstddef>
#include <cstdint>

#include <gsl/span>

namespace diffy {

// Format + pixel dimensions read straight from an image file header — no decode,
// no dependencies. The floor for image diffing: always available, so even when
// no decoder or terminal image protocol exists we can say what changed.
struct ImageInfo {
    bool ok = false;          // recognized as an image we can describe
    const char* format = "";  // "png" / "jpeg" / "gif" / "bmp" / "webp" / "tiff"
    int width = -1;           // -1 when unknown (recognized format, unreadable dims)
    int height = -1;
};

// Fast magic-byte check for routing (does this look like a raster image?).
bool
looks_image(gsl::span<const uint8_t> data);

// Parse format + dimensions from the header. ok=false if unrecognized.
ImageInfo
image_probe(gsl::span<const uint8_t> data);

}  // namespace diffy
