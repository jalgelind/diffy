// Unit tests for the remote-URL parser and the provider registry. Pure checks
// only — no network and no real providers (the fake plugins' `make` returns
// nullptr). Verifies parse_remote_url across the three git remote shapes and a
// mix of hosts, and the registry's first-match / id-override resolution rules.

#include "review/registry.hpp"

#include <doctest.h>

using namespace diffy::review;

TEST_CASE("parse_remote_url handles the https form and strips .git") {
    auto u = parse_remote_url("https://github.com/octocat/Hello-World.git");
    REQUIRE(u.has_value());
    CHECK(u->host == "github.com");
    CHECK(u->owner == "octocat");
    CHECK(u->repo == "Hello-World");

    auto no_git = parse_remote_url("https://gitlab.com/group/project");
    REQUIRE(no_git.has_value());
    CHECK(no_git->host == "gitlab.com");
    CHECK(no_git->owner == "group");
    CHECK(no_git->repo == "project");
}

TEST_CASE("parse_remote_url handles https with userinfo") {
    auto u = parse_remote_url("https://user@bitbucket.org/team/repo.git");
    REQUIRE(u.has_value());
    CHECK(u->host == "bitbucket.org");
    CHECK(u->owner == "team");
    CHECK(u->repo == "repo");
}

TEST_CASE("parse_remote_url handles the scp-like git@host:owner/repo form") {
    auto u = parse_remote_url("git@github.com:octocat/Hello-World.git");
    REQUIRE(u.has_value());
    CHECK(u->host == "github.com");
    CHECK(u->owner == "octocat");
    CHECK(u->repo == "Hello-World");

    // A self-hosted host, no .git suffix.
    auto corp = parse_remote_url("git@git.corp.example.com:platform/diffy");
    REQUIRE(corp.has_value());
    CHECK(corp->host == "git.corp.example.com");
    CHECK(corp->owner == "platform");
    CHECK(corp->repo == "diffy");
}

TEST_CASE("parse_remote_url handles the ssh:// form with a port") {
    auto u = parse_remote_url("ssh://git@bitbucket.org:22/workspace/repo.git");
    REQUIRE(u.has_value());
    CHECK(u->host == "bitbucket.org");
    CHECK(u->owner == "workspace");
    CHECK(u->repo == "repo");

    auto no_port = parse_remote_url("ssh://git@git.corp.example.com/team/svc.git");
    REQUIRE(no_port.has_value());
    CHECK(no_port->host == "git.corp.example.com");
    CHECK(no_port->owner == "team");
    CHECK(no_port->repo == "svc");
}

TEST_CASE("parse_remote_url covers the four representative hosts") {
    struct Case {
        const char* url;
        const char* host;
    };
    const Case cases[] = {
        {"https://github.com/o/r.git", "github.com"},
        {"https://gitlab.com/o/r.git", "gitlab.com"},
        {"https://bitbucket.org/o/r.git", "bitbucket.org"},
        {"https://git.corp.example.com/o/r.git", "git.corp.example.com"},
    };
    for (const auto& c : cases) {
        auto u = parse_remote_url(c.url);
        REQUIRE(u.has_value());
        CHECK(u->host == c.host);
        CHECK(u->owner == "o");
        CHECK(u->repo == "r");
    }
}

TEST_CASE("parse_remote_url returns nullopt for garbage") {
    CHECK_FALSE(parse_remote_url("").has_value());
    CHECK_FALSE(parse_remote_url("not-a-url").has_value());
    CHECK_FALSE(parse_remote_url("https://github.com/onlyowner").has_value());
}

namespace {

// Build a fake plugin that matches when `needle` is a substring of the host and
// whose factory is a no-op (returns nullptr) — enough to exercise resolution.
ProviderPlugin
fake_plugin(std::string id, std::string needle) {
    ProviderPlugin p;
    p.id = std::move(id);
    const std::string n = std::move(needle);
    p.matches = [n](const RemoteUrl& u) {
        return u.host.find(n) != std::string::npos;
    };
    p.make = [](const RemoteConfig&, HttpClient&,
                const Credential&) -> std::unique_ptr<ReviewProvider> {
        return nullptr;
    };
    return p;
}

}  // namespace

TEST_CASE("ProviderRegistry resolves by host, honors override, and misses cleanly") {
    ProviderRegistry reg;
    reg.register_plugin(fake_plugin("github", "github.com"));
    reg.register_plugin(fake_plugin("bitbucket-cloud", "bitbucket.org"));

    SUBCASE("picks the right plugin by host") {
        RemoteUrl gh{"github.com", "o", "r"};
        const ProviderPlugin* p = reg.resolve(gh);
        REQUIRE(p != nullptr);
        CHECK(p->id == "github");

        RemoteUrl bb{"bitbucket.org", "o", "r"};
        const ProviderPlugin* pb = reg.resolve(bb);
        REQUIRE(pb != nullptr);
        CHECK(pb->id == "bitbucket-cloud");
    }

    SUBCASE("an override by provider_id wins over host matching") {
        RemoteUrl gh{"github.com", "o", "r"};  // host would match "github"
        RemoteConfig ovr;
        ovr.provider_id = "bitbucket-cloud";  // but force bitbucket
        const ProviderPlugin* p = reg.resolve(gh, ovr);
        REQUIRE(p != nullptr);
        CHECK(p->id == "bitbucket-cloud");
    }

    SUBCASE("an override with an empty provider_id falls back to host matching") {
        RemoteUrl gh{"github.com", "o", "r"};
        RemoteConfig ovr;  // empty provider_id
        const ProviderPlugin* p = reg.resolve(gh, ovr);
        REQUIRE(p != nullptr);
        CHECK(p->id == "github");
    }

    SUBCASE("an unknown host resolves to nullptr") {
        RemoteUrl unknown{"git.corp.example.com", "o", "r"};
        CHECK(reg.resolve(unknown) == nullptr);
    }

    SUBCASE("an override naming an unregistered provider resolves to nullptr") {
        RemoteUrl gh{"github.com", "o", "r"};
        RemoteConfig ovr;
        ovr.provider_id = "gitlab";  // not registered
        CHECK(reg.resolve(gh, ovr) == nullptr);
    }
}
