#include <doctest.h>

#include "image/image_info.hpp"

#include <cstdint>
#include <vector>

using namespace diffy;

namespace {
gsl::span<const uint8_t>
sp(const std::vector<uint8_t>& v) {
    return gsl::span<const uint8_t>(v.data(), static_cast<std::ptrdiff_t>(v.size()));
}
}  // namespace

TEST_CASE("image_probe reads PNG dimensions from IHDR") {
    // 8-byte sig, then IHDR len(0x0D), "IHDR", width=16, height=32.
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                0x00, 0x00, 0x00, 0x0D, 'I',  'H',  'D',  'R',
                                0x00, 0x00, 0x00, 0x10,  // width 16
                                0x00, 0x00, 0x00, 0x20,  // height 32
                                0x08, 0x06, 0x00, 0x00, 0x00};
    CHECK(looks_image(sp(png)));
    const auto info = image_probe(sp(png));
    CHECK(info.ok);
    CHECK(std::string(info.format) == "png");
    CHECK(info.width == 16);
    CHECK(info.height == 32);
}

TEST_CASE("image_probe reads GIF and BMP dimensions") {
    std::vector<uint8_t> gif = {'G', 'I', 'F', '8', '9', 'a', 0x0A, 0x00, 0x14, 0x00};  // 10x20
    auto gi = image_probe(sp(gif));
    CHECK(gi.ok);
    CHECK(std::string(gi.format) == "gif");
    CHECK(gi.width == 10);
    CHECK(gi.height == 20);

    std::vector<uint8_t> bmp(26, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[18] = 0x64;  // width 100
    bmp[22] = 0x32;  // height 50
    auto bi = image_probe(sp(bmp));
    CHECK(bi.ok);
    CHECK(std::string(bi.format) == "bmp");
    CHECK(bi.width == 100);
    CHECK(bi.height == 50);
}

TEST_CASE("image_probe reads JPEG dimensions from SOF0") {
    // SOI, APP0 (len 4, 2 payload), then SOF0 len 17, precision, height=48, width=64.
    std::vector<uint8_t> jpg = {0xFF, 0xD8,                    // SOI
                                0xFF, 0xE0, 0x00, 0x04, 0, 0,  // APP0 seg len 4
                                0xFF, 0xC0, 0x00, 0x11, 0x08,  // SOF0, len 17, precision 8
                                0x00, 0x30,                    // height 48
                                0x00, 0x40};                   // width 64
    auto info = image_probe(sp(jpg));
    CHECK(info.ok);
    CHECK(std::string(info.format) == "jpeg");
    CHECK(info.height == 48);
    CHECK(info.width == 64);
}

TEST_CASE("image_probe: SOF truncated right after height stays in bounds (IMG-1)") {
    // A SOF0 whose width field's second byte would be one past the buffer. The old
    // guard (i+7 < size) admitted this and read d[i+8] out of bounds (span abort).
    std::vector<uint8_t> jpg = {0xFF, 0xD8,        // SOI
                                0xFF, 0xC0,        // SOF0
                                0x00, 0x11, 0x08,  // seg len, precision
                                0x00, 0x30,        // height 48 (i+5, i+6)
                                0x00};             // width high byte only; its low byte is OOB
    const auto info = image_probe(sp(jpg));        // must not read past the end
    CHECK(info.ok);
    CHECK(std::string(info.format) == "jpeg");
    CHECK(info.width == -1);   // SOF couldn't be fully read -> dims left unknown
    CHECK(info.height == -1);
}

TEST_CASE("image_probe: PNG whose first chunk isn't IHDR yields no dimensions (IMG-1)") {
    std::vector<uint8_t> png = {0x89, 'P',  'N',  'G',  0x0D, 0x0A, 0x1A, 0x0A,
                                0x00, 0x00, 0x00, 0x0D, 'g',  'A',  'M',  'A',  // not IHDR
                                0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20};
    const auto info = image_probe(sp(png));
    CHECK(info.ok);
    CHECK(info.width == -1);
    CHECK(info.height == -1);
}

TEST_CASE("looks_image rejects non-images and short input") {
    std::vector<uint8_t> text = {'h', 'e', 'l', 'l', 'o'};
    CHECK_FALSE(looks_image(sp(text)));
    CHECK_FALSE(image_probe(sp(text)).ok);

    std::vector<uint8_t> tiny = {0x89, 'P'};  // truncated PNG magic
    CHECK_FALSE(looks_image(sp(tiny)));
}

TEST_CASE("image_probe is safe on a truncated header (format known, dims unknown)") {
    // Valid PNG signature but no IHDR payload — must not read past the end.
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const auto info = image_probe(sp(png));
    CHECK(info.ok);
    CHECK(std::string(info.format) == "png");
    CHECK(info.width == -1);
    CHECK(info.height == -1);
}
