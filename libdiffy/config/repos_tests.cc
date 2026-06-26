#include <doctest.h>

#include "config/repos.hpp"

#include <string>
#include <vector>

using namespace diffy;

namespace {

const RepoEntry*
find_by_name(const std::vector<RepoEntry>& v, const std::string& name) {
    for (const auto& r : v) {
        if (r.name == name) {
            return &r;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("repos_add promotes most-recent and de-duplicates") {
    std::vector<RepoEntry> v;
    repos_add(v, "/tmp/alpha");
    repos_add(v, "/tmp/beta");
    REQUIRE(v.size() == 2);
    CHECK(v.front().name == "beta");

    // Re-adding an existing repo moves it to the front without growing the list.
    repos_add(v, "/tmp/alpha");
    REQUIRE(v.size() == 2);
    CHECK(v.front().name == "alpha");
}

TEST_CASE("repos pin survives re-add, remove works") {
    std::vector<RepoEntry> v;
    repos_add(v, "/tmp/alpha");
    repos_add(v, "/tmp/beta");

    repos_set_pinned(v, "/tmp/alpha", true);
    {
        const auto* a = find_by_name(v, "alpha");
        REQUIRE(a != nullptr);
        CHECK(a->pinned);
    }

    // Pin state carries across a promote.
    repos_add(v, "/tmp/alpha");
    {
        const auto* a = find_by_name(v, "alpha");
        REQUIRE(a != nullptr);
        CHECK(a->pinned);
    }

    repos_remove(v, "/tmp/beta");
    CHECK(v.size() == 1);
    CHECK(find_by_name(v, "beta") == nullptr);
    CHECK(find_by_name(v, "alpha") != nullptr);
}
