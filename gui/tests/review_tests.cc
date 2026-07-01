// Proves the Slint-free diffy_review lib (and its vendored JSON dep) links into
// the non-Slint logic-test binary. Real provider/model tests replace this as the
// P0/P0.5 tasks land.

#include "review/review.hpp"

#include <doctest.h>

TEST_CASE("review library links and reports its json version") {
    const std::string v = diffy::review::library_version();
    CHECK(v.find("diffy-review") != std::string::npos);
    CHECK(v.find("json 3.11") != std::string::npos);
}
