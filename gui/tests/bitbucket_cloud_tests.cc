// Bitbucket Cloud provider — fixture-driven tests (no network). Recorded
// Bitbucket REST 2.0 JSON is served via MockHttpClient; we assert the provider
// (1) passes the shared conformance battery and (2) maps each entity onto the
// neutral model correctly, plus pagination, auth header, and error normalization.

#include "review/conformance.hpp"
#include "review/mock_http_client.hpp"
#include "review/providers/bitbucket_cloud.hpp"

#include <doctest.h>

using namespace diffy::review;

namespace {

Credential basic_cred() {
    Credential c;
    c.method = AuthMethod::BasicToken;
    c.principal = "alice";
    c.secret = "app-pass";
    return c;
}

HttpResponse ok_json(const std::string& body) {
    HttpResponse r;
    r.status = 200;
    r.body = body;
    return r;
}

// Register a full, consistent PR #1 fixture set. More-specific URL substrings are
// registered BEFORE the generic "/pullrequests/1" so MockHttpClient's first-match
// rule routes diffstat/commits/comments correctly.
void seed(MockHttpClient& m) {
    m.on("/user", ok_json(R"({"account_id":"acc-1","nickname":"alice","display_name":"Alice A"})"));

    m.on("pullrequests?state=OPEN", ok_json(R"({"values":[
      {"id":1,"title":"Fix race","description":"the body","state":"OPEN","comment_count":2,
       "updated_on":"2026-06-01T10:00:00Z","author":{"display_name":"Alice A"},
       "source":{"branch":{"name":"feat/x"},"commit":{"hash":"aaaa1111"}},
       "destination":{"branch":{"name":"master"},"commit":{"hash":"bbbb2222"}}},
      {"id":2,"title":"Docs","state":"OPEN","author":{"display_name":"Bob"},
       "source":{"branch":{"name":"docs"}},"destination":{"branch":{"name":"master"}}}
    ]})"));

    m.on("/pullrequests/1/diffstat", ok_json(R"({"values":[
      {"status":"modified","lines_added":10,"lines_removed":3,"old":{"path":"src/a.cc"},"new":{"path":"src/a.cc"}},
      {"status":"renamed","lines_added":0,"lines_removed":0,"old":{"path":"old.txt"},"new":{"path":"new.txt"}},
      {"status":"removed","lines_added":0,"lines_removed":5,"old":{"path":"gone.txt"},"new":null}
    ]})"));

    m.on("/pullrequests/1/commits", ok_json(R"({"values":[
      {"hash":"aaaa1111bbbb","message":"Fix race\n\ndetails","date":"2026-06-01T09:00:00Z","author":{"user":{"display_name":"Alice A"}}},
      {"hash":"cccc3333","message":"Add test","date":"2026-06-01T09:30:00Z","author":{"raw":"Bob <b@x>"}}
    ]})"));

    m.on("/pullrequests/1/comments", ok_json(R"({"values":[
      {"id":100,"content":{"raw":"needs a guard"},"user":{"display_name":"Alice A"},"created_on":"2026-06-01T11:00:00Z","inline":{"path":"src/a.cc","to":42}},
      {"id":101,"content":{"raw":"agreed"},"user":{"display_name":"Bob"},"created_on":"2026-06-01T11:05:00Z","parent":{"id":100}},
      {"id":102,"content":{"raw":"general note"},"user":{"display_name":"Alice A"},"created_on":"2026-06-01T11:10:00Z"}
    ]})"));

    m.on("/pullrequests/1", ok_json(R"({"id":1,"title":"Fix race","description":"the body","state":"OPEN",
      "comment_count":2,"updated_on":"2026-06-01T10:00:00Z","author":{"display_name":"Alice A"},
      "source":{"branch":{"name":"feat/x"},"commit":{"hash":"aaaa1111"}},
      "destination":{"branch":{"name":"master"},"commit":{"hash":"bbbb2222"}}})"));
}

}  // namespace

TEST_CASE("Bitbucket Cloud passes the shared conformance battery") {
    MockHttpClient mock;
    seed(mock);
    BitbucketCloudClient client(mock, basic_cred(), "ws", "repo");

    ConformanceReport report = run_conformance(client, "1");
    std::string joined;
    for (const auto& f : report.failures) {
        joined += "\n  - " + f;
    }
    CHECK_MESSAGE(report.failed == 0, "conformance failures:" << joined);
}

TEST_CASE("Bitbucket Cloud maps entities onto the neutral model") {
    MockHttpClient mock;
    seed(mock);
    BitbucketCloudClient client(mock, basic_cred(), "ws", "repo");

    SUBCASE("capabilities") {
        Capabilities c = client.capabilities();
        CHECK(c.item_noun == "pull request");
        CHECK(c.granularity == CommentGranularity::Line);
        CHECK_FALSE(c.request_changes_state);
    }

    SUBCASE("list_open") {
        auto r = client.list_open();
        REQUIRE(r.has_value());
        CHECK(r.value().items.size() == 2);
        CHECK(r.value().items[0].id == "1");
        CHECK(r.value().items[0].title == "Fix race");
        CHECK(r.value().items[0].src_branch == "feat/x");
        CHECK(r.value().items[0].dst_branch == "master");
        CHECK(r.value().items[0].state == ApprovalState::Open);
        CHECK_FALSE(r.value().has_more);
    }

    SUBCASE("get + refs") {
        auto pr = client.get("1");
        REQUIRE(pr.has_value());
        CHECK(pr.value().id == "1");
        CHECK(pr.value().description == "the body");
        CHECK(pr.value().author == "Alice A");

        auto rf = client.refs("1");
        REQUIRE(rf.has_value());
        CHECK(rf.value().head_sha == "aaaa1111");
        CHECK(rf.value().base_sha == "bbbb2222");
        CHECK(rf.value().base_ref == "master");
        CHECK(rf.value().head_ref == "refs/pull-requests/1/from");
    }

    SUBCASE("files (diffstat) incl. rename + delete") {
        auto r = client.files("1");
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() == 3);
        CHECK(r.value()[0].status == "modified");
        CHECK(r.value()[0].additions == 10);
        CHECK(r.value()[0].deletions == 3);
        CHECK(r.value()[1].status == "renamed");
        CHECK(r.value()[1].path == "new.txt");
        REQUIRE(r.value()[1].old_path.has_value());
        CHECK(r.value()[1].old_path.value() == "old.txt");
        CHECK(r.value()[2].status == "deleted");
        CHECK(r.value()[2].path == "gone.txt");
    }

    SUBCASE("commits") {
        auto r = client.commits("1");
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() == 2);
        CHECK(r.value()[0].sha == "aaaa1111bbbb");
        CHECK(r.value()[0].short_sha == "aaaa1111");
        CHECK(r.value()[0].summary == "Fix race");
        CHECK(r.value()[0].author == "Alice A");
        CHECK(r.value()[1].author == "Bob <b@x>");
    }

    SUBCASE("threads group replies under an inline root; general is anchorless") {
        auto r = client.threads("1");
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() == 2);
        const ReviewThread& t0 = r.value()[0];
        CHECK(t0.id == "100");
        CHECK(t0.anchor.new_path == "src/a.cc");
        CHECK(t0.anchor.side == Side::New);
        CHECK(t0.anchor.start.line == 42);
        CHECK(t0.comments.size() == 2);  // root + reply
        const ReviewThread& t1 = r.value()[1];
        CHECK(t1.anchor.new_path.empty());  // general comment
        CHECK(t1.comments.size() == 1);
    }

    SUBCASE("request_changes is Unsupported on Bitbucket Cloud") {
        auto r = client.request_changes("1");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == ErrorKind::Unsupported);
    }

    SUBCASE("sends HTTP Basic auth") {
        client.get("1");
        REQUIRE_FALSE(mock.sent.empty());
        bool saw_basic = false;
        for (const auto& [n, v] : mock.sent.back().headers) {
            if (n == "Authorization" && v.rfind("Basic ", 0) == 0) {
                saw_basic = true;
            }
        }
        CHECK(saw_basic);
    }
}

TEST_CASE("Bitbucket Cloud follows next-page cursors") {
    MockHttpClient mock;
    // Register the page-2 rule first so the generic OPEN rule doesn't shadow it.
    mock.on("page=2", ok_json(R"({"values":[{"id":3,"title":"Third","state":"OPEN"}]})"));
    mock.on("pullrequests?state=OPEN", ok_json(R"({"values":[{"id":1,"title":"First","state":"OPEN"}],
      "next":"https://api.bitbucket.org/2.0/repositories/ws/repo/pullrequests?state=OPEN&page=2"})"));
    BitbucketCloudClient client(mock, basic_cred(), "ws", "repo");

    auto p1 = client.list_open();
    REQUIRE(p1.has_value());
    CHECK(p1.value().items.size() == 1);
    CHECK(p1.value().has_more);
    REQUIRE_FALSE(p1.value().next_cursor.empty());

    auto p2 = client.list_open(p1.value().next_cursor);
    REQUIRE(p2.has_value());
    CHECK(p2.value().items.size() == 1);
    CHECK(p2.value().items[0].id == "3");
    CHECK_FALSE(p2.value().has_more);
}

TEST_CASE("Bitbucket Cloud normalizes HTTP + transport errors") {
    SUBCASE("401 -> Auth") {
        MockHttpClient mock;
        HttpResponse r;
        r.status = 401;
        r.body = R"({"error":{"message":"no"}})";
        mock.on("/user", r);
        BitbucketCloudClient client(mock, basic_cred(), "ws", "repo");
        auto who = client.whoami();
        REQUIRE_FALSE(who.has_value());
        CHECK(who.error().kind == ErrorKind::Auth);
    }
    SUBCASE("429 -> RateLimited with retry-after") {
        MockHttpClient mock;
        HttpResponse r;
        r.status = 429;
        r.headers = {{"Retry-After", "17"}};
        mock.on("/user", r);
        BitbucketCloudClient client(mock, basic_cred(), "ws", "repo");
        auto who = client.whoami();
        REQUIRE_FALSE(who.has_value());
        CHECK(who.error().kind == ErrorKind::RateLimited);
        REQUIRE(who.error().retry_after_secs.has_value());
        CHECK(who.error().retry_after_secs.value() == 17);
    }
    SUBCASE("transport failure -> Network") {
        MockHttpClient mock;
        mock.on_error("/user", HttpError::Tls, "handshake failed");
        BitbucketCloudClient client(mock, basic_cred(), "ws", "repo");
        auto who = client.whoami();
        REQUIRE_FALSE(who.has_value());
        CHECK(who.error().kind == ErrorKind::Network);
    }
}
