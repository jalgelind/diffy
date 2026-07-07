#include "image/image_info.hpp"

#include <cstring>

namespace diffy {

namespace {

using Bytes = gsl::span<const uint8_t>;

bool
starts_with(Bytes d, const char* sig, size_t n) {
    if (d.size() < n) {
        return false;
    }
    return std::memcmp(d.data(), sig, n) == 0;
}

uint32_t
be32(Bytes d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) | (uint32_t(d[o + 2]) << 8) |
           uint32_t(d[o + 3]);
}
uint16_t
be16(Bytes d, size_t o) {
    return uint16_t((uint32_t(d[o]) << 8) | uint32_t(d[o + 1]));
}
uint16_t
le16(Bytes d, size_t o) {
    return uint16_t(uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8));
}
int32_t
le32(Bytes d, size_t o) {
    return int32_t(uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) |
                  (uint32_t(d[o + 3]) << 24));
}

const uint8_t kPng[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

bool
is_png(Bytes d) {
    return d.size() >= 8 && std::memcmp(d.data(), kPng, 8) == 0;
}
bool
is_jpeg(Bytes d) {
    return d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}
bool
is_gif(Bytes d) {
    return starts_with(d, "GIF87a", 6) || starts_with(d, "GIF89a", 6);
}
bool
is_bmp(Bytes d) {
    return d.size() >= 2 && d[0] == 'B' && d[1] == 'M';
}
bool
is_webp(Bytes d) {
    return d.size() >= 12 && std::memcmp(d.data(), "RIFF", 4) == 0 &&
           std::memcmp(d.data() + 8, "WEBP", 4) == 0;
}
bool
is_tiff(Bytes d) {
    return d.size() >= 4 && ((d[0] == 'I' && d[1] == 'I' && d[2] == 0x2A && d[3] == 0x00) ||
                            (d[0] == 'M' && d[1] == 'M' && d[2] == 0x00 && d[3] == 0x2A));
}

// Scan JPEG segments for a Start-Of-Frame marker to read dimensions.
void
jpeg_dims(Bytes d, int& w, int& h) {
    size_t i = 2;  // past SOI (FF D8)
    while (i + 1 < d.size()) {
        if (d[i] != 0xFF) {
            ++i;
            continue;
        }
        const uint8_t marker = d[i + 1];
        // Standalone markers (no length): padding 0xFF, RSTn (D0-D7), SOI/EOI, TEM.
        if (marker == 0xFF || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
            i += 2;
            continue;
        }
        if (i + 3 >= d.size()) {
            return;
        }
        const uint16_t seg_len = be16(d, i + 2);
        // SOF0..SOF15 carry dimensions, except DHT(C4)/JPG(C8)/DAC(CC).
        const bool is_sof =
            marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (is_sof && i + 8 < d.size()) {  // be16(d, i+7) reads d[i+8], so need i+8 in range
            h = be16(d, i + 5);
            w = be16(d, i + 7);
            return;
        }
        if (seg_len < 2) {
            return;  // malformed
        }
        i += 2 + seg_len;
    }
}

void
webp_dims(Bytes d, int& w, int& h) {
    if (d.size() < 16) {
        return;
    }
    const uint8_t* p = d.data();
    if (std::memcmp(p + 12, "VP8X", 4) == 0) {
        if (d.size() >= 30) {
            const uint32_t cw = uint32_t(d[24]) | (uint32_t(d[25]) << 8) | (uint32_t(d[26]) << 16);
            const uint32_t ch = uint32_t(d[27]) | (uint32_t(d[28]) << 8) | (uint32_t(d[29]) << 16);
            w = int(cw + 1);
            h = int(ch + 1);
        }
    } else if (std::memcmp(p + 12, "VP8 ", 4) == 0) {
        // Lossy keyframe: dimensions at offset 26/28 (14-bit).
        if (d.size() >= 30) {
            w = le16(d, 26) & 0x3FFF;
            h = le16(d, 28) & 0x3FFF;
        }
    } else if (std::memcmp(p + 12, "VP8L", 4) == 0) {
        // Lossless: 0x2F signature at 20, then 14-bit width-1, 14-bit height-1.
        if (d.size() >= 25 && d[20] == 0x2F) {
            const uint32_t bits = uint32_t(d[21]) | (uint32_t(d[22]) << 8) | (uint32_t(d[23]) << 16) |
                                  (uint32_t(d[24]) << 24);
            w = int((bits & 0x3FFF) + 1);
            h = int(((bits >> 14) & 0x3FFF) + 1);
        }
    }
}

}  // namespace

bool
looks_image(Bytes d) {
    return is_png(d) || is_jpeg(d) || is_gif(d) || is_bmp(d) || is_webp(d) || is_tiff(d);
}

ImageInfo
image_probe(Bytes d) {
    ImageInfo info;
    if (is_png(d)) {
        info.ok = true;
        info.format = "png";
        // 8 sig + 4 len + 4 "IHDR" + 4 w + 4 h. Verify the IHDR tag so a PNG whose
        // first chunk isn't IHDR doesn't have unrelated bytes read as dimensions.
        if (d.size() >= 24 && std::memcmp(d.data() + 12, "IHDR", 4) == 0) {
            info.width = int(be32(d, 16));
            info.height = int(be32(d, 20));
        }
    } else if (is_jpeg(d)) {
        info.ok = true;
        info.format = "jpeg";
        jpeg_dims(d, info.width, info.height);
    } else if (is_gif(d)) {
        info.ok = true;
        info.format = "gif";
        if (d.size() >= 10) {
            info.width = le16(d, 6);
            info.height = le16(d, 8);
        }
    } else if (is_bmp(d)) {
        info.ok = true;
        info.format = "bmp";
        if (d.size() >= 26) {
            const int32_t ww = le32(d, 18);
            info.width = ww < 0 ? -ww : ww;  // clamp; width should never be negative
            const int32_t hh = le32(d, 22);
            info.height = hh < 0 ? -hh : hh;  // negative = top-down
        }
    } else if (is_webp(d)) {
        info.ok = true;
        info.format = "webp";
        webp_dims(d, info.width, info.height);
    } else if (is_tiff(d)) {
        info.ok = true;
        info.format = "tiff";  // dimensions require walking the IFD; left unknown
    }
    return info;
}

}  // namespace diffy
