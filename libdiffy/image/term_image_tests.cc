#include <doctest.h>

#include "image/term_image.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace diffy;

namespace {
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
int
count(const std::string& s, const std::string& sub) {
    int n = 0;
    for (size_t p = s.find(sub); p != std::string::npos; p = s.find(sub, p + sub.size())) {
        ++n;
    }
    return n;
}
}  // namespace

TEST_CASE("detect_term_image_protocol: tty/force enable, disabled/pipe don't") {
    TermEnv e;
    e.is_tty = true;
    CHECK(detect_term_image_protocol(e) == TermImageProtocol::HalfBlock);

    e = {};
    e.force = true;  // render even to a pipe
    CHECK(detect_term_image_protocol(e) == TermImageProtocol::HalfBlock);

    e = {};  // not a tty, not forced
    CHECK(detect_term_image_protocol(e) == TermImageProtocol::None);

    e = {};
    e.is_tty = true;
    e.disabled = true;  // --no-image-render wins
    CHECK(detect_term_image_protocol(e) == TermImageProtocol::None);
}

TEST_CASE("render_halfblock emits well-formed truecolor half-block lines") {
    const auto img = solid(8, 8, 200, 40, 40);
    const std::string art = render_halfblock(img, 8, 8, 8, 4);
    REQUIRE_FALSE(art.empty());
    CHECK(art.find("▀") != std::string::npos);          // upper-half-block glyph
    CHECK(art.find("\033[38;2;") != std::string::npos);  // truecolor foreground
    CHECK(art.find(";48;2;") != std::string::npos);      // truecolor background (same SGR)
    CHECK(art.find("\033[0m") != std::string::npos);     // reset per line
    // 8px tall @ 2px/cell => 4 cell rows => 4 resets (one per line).
    CHECK(count(art, "\033[0m") == 4);
}

TEST_CASE("render_halfblock fits within the cell budget and rejects bad input") {
    const auto img = solid(100, 100, 10, 20, 30);
    const std::string art = render_halfblock(img, 100, 100, 10, 5);
    // At most max_rows (5) cell rows.
    CHECK(count(art, "\033[0m") <= 5);

    CHECK(render_halfblock({}, 0, 0, 10, 10).empty());
    CHECK(render_halfblock(img, 100, 100, 0, 0).empty());
}
