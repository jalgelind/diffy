// Compile + link coverage for the diffy_review scaffolding: the neutral model,
// Result/Page, capabilities, the HTTP client factory (constructed, not called),
// the secret-store key builder, and the Link-header pager. Pure checks only — no
// network and no writes to the OS credential vault. Real provider/model tests
// replace this as the P0.5 tasks land.

#include "review/capabilities.hpp"
#include "review/http_client.hpp"
#include "review/link_header.hpp"
#include "review/model.hpp"
#include "review/result.hpp"
#include "review/review.hpp"
#include "review/review_provider.hpp"
#include "review/secret_store.hpp"

#include <doctest.h>

using namespace diffy::review;

TEST_CASE("review library links and reports its json version") {
    const std::string v = library_version();
    CHECK(v.find("diffy-review") != std::string::npos);
    CHECK(v.find("json 3.11") != std::string::npos);
}

TEST_CASE("Result carries value or normalized error") {
    Result<int> ok = Result<int>::ok(42);
    CHECK(ok.has_value());
    CHECK(ok.value() == 42);

    Result<int> bad = Result<int>::err({ErrorKind::RateLimited, 429, "slow down", 30});
    CHECK_FALSE(bad.has_value());
    CHECK(bad.error().kind == ErrorKind::RateLimited);
    CHECK(bad.error().retry_after_secs.value() == 30);

    CHECK(Result<void>::ok().has_value());
}

TEST_CASE("RangeAnchor defaults to a safe whole-line New-side anchor") {
    RangeAnchor a;
    CHECK(a.side == Side::New);
    CHECK_FALSE(a.start.col.has_value());
}

TEST_CASE("build_key composes a stable, namespaced credential key") {
    CHECK(build_key("bitbucket-cloud", "https://api.bitbucket.org", "alice") ==
          "diffy-review/bitbucket-cloud/https://api.bitbucket.org/alice");
}

TEST_CASE("Link-header parser extracts rel=next") {
    const std::string h =
        "<https://api.example.com/prs?page=2>; rel=\"next\", "
        "<https://api.example.com/prs?page=9>; rel=\"last\"";
    CHECK(next_link(h) == "https://api.example.com/prs?page=2");
    CHECK(next_link("<https://x/>; rel=\"prev\"").empty());
}

TEST_CASE("HTTP client factory returns a backend") {
    auto client = make_http_client();
    CHECK(client != nullptr);
}
