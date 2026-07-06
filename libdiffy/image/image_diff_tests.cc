#include <doctest.h>

#include "image/image_diff.hpp"

#include <cstdint>
#include <vector>

using namespace diffy;

namespace {
// Solid w*h RGBA image of one colour.
std::vector<uint8_t>
solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> v(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < v.size(); i += 4) {
        v[i] = r;
        v[i + 1] = g;
        v[i + 2] = b;
        v[i + 3] = 255;
    }
    return v;
}
}  // namespace

TEST_CASE("image_diff: identical images are 100% similar, 0 changed") {
    auto a = solid(8, 8, 10, 20, 30);
    auto r = image_diff(a, a, 8, 8);
    CHECK(r.comparable);
    CHECK(r.changed_px == 0);
    CHECK(r.similarity == doctest::Approx(1.0));
    CHECK(r.total_px == 64);
    CHECK(r.overlay_rgba.size() == 8u * 8u * 4u);
}

TEST_CASE("image_diff: a single flipped pixel counts as one change") {
    auto a = solid(4, 4, 0, 0, 0);
    auto b = a;
    // Flip the center pixel (2,2) to white — a lone changed pixel with no
    // flat-region siblings won't be mistaken for anti-aliasing.
    const size_t pos = (2u * 4 + 2) * 4;
    b[pos] = 255;
    b[pos + 1] = 255;
    b[pos + 2] = 255;
    auto r = image_diff(a, b, 4, 4);
    CHECK(r.comparable);
    CHECK(r.changed_px == 1);
    // The overlay marks that pixel magenta.
    CHECK(r.overlay_rgba[pos] == 255);
    CHECK(r.overlay_rgba[pos + 1] == 0);
    CHECK(r.overlay_rgba[pos + 2] == 255);
}

TEST_CASE("image_diff: a big solid-colour shift changes every pixel") {
    auto a = solid(6, 6, 0, 0, 0);
    auto b = solid(6, 6, 255, 255, 255);
    auto r = image_diff(a, b, 6, 6);
    CHECK(r.comparable);
    CHECK(r.changed_px == 36);
    CHECK(r.similarity == doctest::Approx(0.0));
}

TEST_CASE("image_diff: mismatched buffers are not comparable") {
    auto a = solid(4, 4, 1, 2, 3);
    std::vector<uint8_t> b(10, 0);  // too small for 4x4
    auto r = image_diff(a, b, 4, 4);
    CHECK_FALSE(r.comparable);
    CHECK(r.overlay_rgba.empty());
}

TEST_CASE("image_diff: threshold tolerates a tiny channel nudge") {
    auto a = solid(4, 4, 100, 100, 100);
    auto b = a;
    for (size_t i = 0; i < b.size(); i += 4) {
        b[i] = 101;  // +1 on red everywhere
    }
    // Default threshold should treat a 1-level nudge as unchanged.
    auto r = image_diff(a, b, 4, 4);
    CHECK(r.comparable);
    CHECK(r.changed_px == 0);
}
