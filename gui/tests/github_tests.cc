// GitHub provider — fixture-driven tests (no network). Recorded GitHub REST v3
// JSON is served via MockHttpClient; we assert the provider (1) passes the SAME
// shared conformance battery as Bitbucket (the P0.5 abstraction proof) and
// (2) maps each entity onto the neutral model correctly, plus Link-header
// pagination, the mandatory headers, and the write operations.

#include "review/conformance.hpp"
#include "review/mock_http_client.hpp"
#include "review/providers/github.hpp"

#include <doctest.h>

#include <algorithm>

using namespace diffy::review;

namespace {

Credential pat_cred() {
    Credential c;
    c.method = AuthMethod::Bearer;
    c.secret = "ghp_token";
    return c;
}

HttpResponse ok_json(const std::string& body) {
    HttpResponse r;
    r.status = 200;
    r.body = body;
    return r;
}

const ReviewThread* find_thread(const std::vector<ReviewThread>& v, const std::string& id) {
    auto it = std::find_if(v.begin(), v.end(), [&](const ReviewThread& t) { return t.id == id; });
    return it == v.end() ? nullptr : &*it;
}

// A full, consistent PR #42 fixture set. More-specific URL substrings register
// BEFORE the generic "/pulls/42" so MockHttpClient's first-match rule routes the
// sub-resources (files/commits/comments) correctly.
void seed(MockHttpClient& m) {
    m.on("/user", ok_json(R"({"id":7,"login":"alice","name":"Alice A"})"));

    m.on("/pulls/42/files", ok_json(R"([
      {"filename":"src/a.cc","status":"modified","additions":10,"deletions":3},
      {"filename":"new.txt","status":"renamed","previous_filename":"old.txt","additions":0,"deletions":0},
      {"filename":"gone.txt","status":"removed","additions":0,"deletions":5},
      {"filename":"changed.txt","status":"changed","additions":1,"deletions":1}
    ])"));

    m.on("/pulls/42/commits", ok_json(R"([
      {"sha":"aaaa1111bbbb","commit":{"message":"Fix race\n\ndetails","author":{"name":"Alice A","date":"2026-06-01T09:00:00Z"}}},
      {"sha":"cccc3333","commit":{"message":"Add test","author":{"name":"Bob","date":"2026-06-01T09:30:00Z"}}}
    ])"));

    m.on("/pulls/42/comments", ok_json(R"([
      {"id":100,"path":"src/a.cc","line":42,"side":"RIGHT","body":"needs a guard","user":{"login":"alice","id":7},"created_at":"2026-06-01T11:00:00Z"},
      {"id":101,"in_reply_to_id":100,"path":"src/a.cc","line":42,"side":"RIGHT","body":"agreed","user":{"login":"bob","id":8},"created_at":"2026-06-01T11:05:00Z"},
      {"id":102,"path":"src/a.cc","line":null,"original_line":10,"side":"LEFT","body":"stale","user":{"login":"alice","id":7},"created_at":"2026-06-01T11:10:00Z"}
    ])"));

    m.on("/issues/42/comments", ok_json(R"([
      {"id":500,"body":"general note","user":{"login":"alice","id":7},"created_at":"2026-06-01T12:00:00Z"}
    ])"));

    m.on("pulls?state=open", ok_json(R"([
      {"number":1,"title":"Fix race","body":"the body","state":"open","comments":1,"review_comments":1,
       "updated_at":"2026-06-01T10:00:00Z","user":{"login":"alice","id":7},"draft":false,"merged":false,
       "head":{"ref":"feat/x","sha":"aaaa1111"},"base":{"ref":"master","sha":"bbbb2222"}},
      {"number":2,"title":"Docs","state":"open","user":{"login":"bob","id":8},
       "head":{"ref":"docs","sha":"dddd"},"base":{"ref":"master","sha":"eeee"}}
    ])"));

    m.on("/pulls/42", ok_json(R"({"number":42,"title":"Fix race","body":"the body","state":"open",
      "comments":1,"review_comments":2,"updated_at":"2026-06-01T10:00:00Z",
      "user":{"login":"alice","id":7},"draft":false,"merged":false,
      "requested_reviewers":[{"login":"bob","id":8,"avatar_url":"https://x/b.png"}],
      "head":{"ref":"feat/x","sha":"aaaa1111"},"base":{"ref":"master","sha":"bbbb2222"}})"));
}

}  // namespace

TEST_CASE("GitHub passes the shared conformance battery") {
    MockHttpClient mock;
    seed(mock);
    GitHubClient client(mock, pat_cred(), "owner", "repo");

    ConformanceReport report = run_conformance(client, "42");
    std::string joined;
    for (const auto& f : report.failures) {
        joined += "\n  - " + f;
    }
    CHECK_MESSAGE(report.failed == 0, "conformance failures:" << joined);
}

TEST_CASE("GitHub sends the mandatory headers") {
    MockHttpClient mock;
    seed(mock);
    GitHubClient client(mock, pat_cred(), "owner", "repo");
    (void)client.whoami();

    REQUIRE_FALSE(mock.sent.empty());
    const HttpRequest& req = mock.sent.back();
    auto has = [&](const std::string& name, const std::string& val) {
        for (const auto& [n, v] : req.headers) {
            if (n == name) {
                return v.find(val) != std::string::npos;
            }
        }
        return false;
    };
    CHECK(has("Authorization", "Bearer ghp_token"));
    CHECK(has("User-Agent", "diffy"));          // GitHub 403s without a User-Agent
    CHECK(has("X-GitHub-Api-Version", "2022"));
}

TEST_CASE("GitHub maps entities onto the neutral model") {
    MockHttpClient mock;
    seed(mock);
    GitHubClient client(mock, pat_cred(), "owner", "repo");

    SUBCASE("capabilities differ from Bitbucket where the backend does") {
        Capabilities c = client.capabilities();
        CHECK(c.granularity == CommentGranularity::LineRange);
        CHECK(c.pending_review_batch);
        CHECK(c.request_changes_state);
        CHECK(c.suggestions);
        CHECK(c.merge_strategies.size() == 3);
    }

    SUBCASE("list_open maps PRs and has no cursor on a single page") {
        auto r = client.list_open();
        REQUIRE(r.has_value());
        REQUIRE(r.value().items.size() == 2);
        CHECK(r.value().items[0].id == "1");
        CHECK(r.value().items[0].title == "Fix race");
        CHECK(r.value().items[1].id == "2");
        CHECK_FALSE(r.value().has_more);
    }

    SUBCASE("get echoes the number as id") {
        auto r = client.get("42");
        REQUIRE(r.has_value());
        CHECK(r.value().id == "42");
        CHECK(r.value().author == "alice");
        CHECK(r.value().src_branch == "feat/x");
        CHECK(r.value().dst_branch == "master");
        CHECK(r.value().state == ApprovalState::Open);
        REQUIRE(r.value().reviewers.size() == 1);
        CHECK(r.value().reviewers[0].name == "bob");
    }

    SUBCASE("refs yields head/base SHAs and the fetchable head ref") {
        auto r = client.refs("42");
        REQUIRE(r.has_value());
        CHECK(r.value().head_sha == "aaaa1111");
        CHECK(r.value().base_sha == "bbbb2222");
        CHECK(r.value().head_ref == "refs/pull/42/head");
    }

    SUBCASE("files normalize status and carry rename old_path") {
        auto r = client.files("42");
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() == 4);
        CHECK(r.value()[0].status == "modified");
        CHECK(r.value()[1].status == "renamed");
        REQUIRE(r.value()[1].old_path.has_value());
        CHECK(r.value()[1].old_path.value() == "old.txt");
        CHECK(r.value()[2].status == "deleted");   // "removed" -> deleted
        CHECK(r.value()[3].status == "modified");  // "changed" -> modified
    }

    SUBCASE("commits carry sha + summary") {
        auto r = client.commits("42");
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() == 2);
        CHECK(r.value()[0].sha == "aaaa1111bbbb");
        CHECK(r.value()[0].summary == "Fix race");
        CHECK(r.value()[0].author == "Alice A");
    }

    SUBCASE("threads group replies, anchor inline, and flag outdated + general") {
        auto r = client.threads("42");
        REQUIRE(r.has_value());
        const auto& threads = r.value();
        // 2 inline threads (root 100 + reply 101, root 102) + 1 general (500).
        REQUIRE(threads.size() == 3);

        const ReviewThread* t100 = find_thread(threads, "100");
        REQUIRE(t100 != nullptr);
        CHECK(t100->anchor.new_path == "src/a.cc");
        CHECK(t100->anchor.side == Side::New);
        CHECK(t100->anchor.start.line == 42);
        CHECK(t100->comments.size() == 2);        // root + its reply
        CHECK_FALSE(t100->outdated);

        const ReviewThread* t102 = find_thread(threads, "102");
        REQUIRE(t102 != nullptr);
        CHECK(t102->anchor.side == Side::Old);     // "LEFT"
        CHECK(t102->anchor.start.line == 10);      // fell back to original_line
        CHECK(t102->outdated);                     // line was null

        const ReviewThread* t500 = find_thread(threads, "500");
        REQUIRE(t500 != nullptr);
        CHECK(t500->anchor.new_path.empty());      // general (no anchor)
    }
}

TEST_CASE("GitHub follows Link-header pagination") {
    MockHttpClient mock;
    HttpResponse p1 = ok_json(R"([{"number":1,"state":"open","user":{"login":"a","id":1},
                                   "head":{"ref":"x","sha":"1"},"base":{"ref":"m","sha":"2"}}])");
    p1.headers.push_back(
        {"Link", "<https://api.github.com/repos/owner/repo/pulls?page=2>; rel=\"next\""});
    mock.on("GET", "pulls?state=open", p1);

    HttpResponse p2 = ok_json(R"([{"number":9,"state":"open","user":{"login":"b","id":2},
                                   "head":{"ref":"y","sha":"3"},"base":{"ref":"m","sha":"4"}}])");
    mock.on("GET", "pulls?page=2", p2);  // no Link -> last page

    GitHubClient client(mock, pat_cred(), "owner", "repo");
    auto first = client.list_open();
    REQUIRE(first.has_value());
    CHECK(first.value().items[0].id == "1");
    REQUIRE(first.value().has_more);
    CHECK_FALSE(first.value().next_cursor.empty());

    auto second = client.list_open(first.value().next_cursor);
    REQUIRE(second.has_value());
    CHECK(second.value().items[0].id == "9");
    CHECK_FALSE(second.value().has_more);
}

TEST_CASE("GitHub write operations issue the right requests") {
    SUBCASE("reply posts a comment with in_reply_to (no inline anchor)") {
        MockHttpClient mock;
        mock.on("POST", "/pulls/42/comments",
                ok_json(R"({"id":200,"body":"me too","in_reply_to_id":100,
                            "user":{"login":"alice","id":7},"created_at":"2026-06-01T12:00:00Z"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        NewComment nc;
        nc.body_md = "me too";
        nc.reply_to = "100";
        auto r = client.comment("42", nc);
        REQUIRE(r.has_value());
        CHECK(r.value().id == "200");
        CHECK(r.value().parent_id == "100");
        CHECK(r.value().author_id == "7");
        const HttpRequest& req = mock.sent.back();
        CHECK(req.body.find("in_reply_to") != std::string::npos);
        CHECK(req.body.find("100") != std::string::npos);
        CHECK(req.body.find("\"path\"") == std::string::npos);  // a reply has no anchor
    }

    SUBCASE("a general comment posts to the issues endpoint") {
        MockHttpClient mock;
        mock.on("POST", "/issues/42/comments",
                ok_json(R"({"id":300,"body":"looks good","user":{"login":"alice","id":7},
                            "created_at":"2026-06-01T12:00:00Z"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        NewComment nc;
        nc.body_md = "looks good";
        nc.general = true;
        auto r = client.comment("42", nc);
        REQUIRE(r.has_value());
        CHECK(r.value().id == "300");
        CHECK(mock.sent.back().url.find("/issues/42/comments") != std::string::npos);
    }

    SUBCASE("an inline comment carries commit_id + path + line + side") {
        MockHttpClient mock;
        mock.on("POST", "/pulls/42/comments",
                ok_json(R"({"id":210,"body":"nit","path":"src/a.cc","line":12,"side":"RIGHT",
                            "user":{"login":"alice","id":7},"created_at":"2026-06-01T12:00:00Z"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        NewComment nc;
        nc.body_md = "nit";
        nc.anchor.new_path = "src/a.cc";
        nc.anchor.side = Side::New;
        nc.anchor.start.line = 12;
        nc.anchor.ctx.head_sha = "aaaa1111";
        auto r = client.comment("42", nc);
        REQUIRE(r.has_value());
        const HttpRequest& req = mock.sent.back();
        CHECK(req.body.find("\"commit_id\":\"aaaa1111\"") != std::string::npos);
        CHECK(req.body.find("\"path\":\"src/a.cc\"") != std::string::npos);
        CHECK(req.body.find("\"side\":\"RIGHT\"") != std::string::npos);
    }

    SUBCASE("edit PATCHes the review comment by its global id") {
        MockHttpClient mock;
        mock.on("PATCH", "/pulls/comments/210",
                ok_json(R"({"id":210,"body":"edited","user":{"login":"alice","id":7},
                            "created_at":"2026-06-01T11:00:00Z"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        auto r = client.edit_comment("42", "210", "edited");
        REQUIRE(r.has_value());
        CHECK(r.value().body_md == "edited");
        CHECK(mock.sent.back().method == "PATCH");
        CHECK(mock.sent.back().url.find("/pulls/comments/210") != std::string::npos);
    }

    SUBCASE("delete issues a DELETE to the review comment") {
        MockHttpClient mock;
        HttpResponse gone;
        gone.status = 204;
        mock.on("DELETE", "/pulls/comments/210", gone);
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        auto r = client.delete_comment("42", "210");
        CHECK(r.has_value());
        CHECK(mock.sent.back().method == "DELETE");
    }

    SUBCASE("approve posts an APPROVE review") {
        MockHttpClient mock;
        mock.on("POST", "/pulls/42/reviews", ok_json(R"({"id":900,"state":"APPROVED"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        auto r = client.approve("42");
        CHECK(r.has_value());
        CHECK(mock.sent.back().body.find("APPROVE") != std::string::npos);
    }

    SUBCASE("request_changes posts a REQUEST_CHANGES review with a body") {
        MockHttpClient mock;
        mock.on("POST", "/pulls/42/reviews", ok_json(R"({"id":901,"state":"CHANGES_REQUESTED"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        auto r = client.request_changes("42");
        CHECK(r.has_value());
        CHECK(mock.sent.back().body.find("REQUEST_CHANGES") != std::string::npos);
        CHECK(mock.sent.back().body.find("\"body\"") != std::string::npos);
    }

    SUBCASE("submit_review batches verdict + inline comments") {
        MockHttpClient mock;
        mock.on("POST", "/pulls/42/reviews", ok_json(R"({"id":902,"state":"APPROVED"})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        Review rv;
        rv.body_md = "Nice work";
        rv.verdict = ApprovalState::Approved;
        NewComment nc;
        nc.body_md = "tidy this";
        nc.anchor.new_path = "src/a.cc";
        nc.anchor.side = Side::New;
        nc.anchor.start.line = 5;
        rv.comments.push_back(nc);
        auto r = client.submit_review("42", rv);
        CHECK(r.has_value());
        const std::string& body = mock.sent.back().body;
        CHECK(body.find("\"event\":\"APPROVE\"") != std::string::npos);
        CHECK(body.find("Nice work") != std::string::npos);
        CHECK(body.find("\"comments\"") != std::string::npos);
        CHECK(body.find("tidy this") != std::string::npos);
    }

    SUBCASE("merge PUTs the mapped merge_method") {
        MockHttpClient mock;
        mock.on("PUT", "/pulls/42/merge", ok_json(R"({"merged":true})"));
        GitHubClient client(mock, pat_cred(), "owner", "repo");
        auto r = client.merge("42", MergeStrategy::Squash, "Squash it");
        CHECK(r.has_value());
        const HttpRequest& req = mock.sent.back();
        CHECK(req.method == "PUT");
        CHECK(req.body.find("\"merge_method\":\"squash\"") != std::string::npos);
        CHECK(req.body.find("Squash it") != std::string::npos);
    }

    SUBCASE("unapprove dismisses the caller's own APPROVED review") {
        MockHttpClient mock;
        mock.on("/user", ok_json(R"({"id":7,"login":"alice","name":"Alice A"})"));
        mock.on("GET", "/pulls/42/reviews",
                ok_json(R"([{"id":800,"state":"COMMENTED","user":{"id":9}},
                            {"id":900,"state":"APPROVED","user":{"id":7}}])"));
        HttpResponse dismissed = ok_json(R"({"id":900,"state":"DISMISSED"})");
        mock.on("PUT", "/pulls/42/reviews/900/dismissals", dismissed);
        GitHubClient client(mock, pat_cred(), "owner", "repo");

        auto r = client.unapprove("42");
        CHECK(r.has_value());
        const HttpRequest& req = mock.sent.back();
        CHECK(req.method == "PUT");
        CHECK(req.url.find("/reviews/900/dismissals") != std::string::npos);
    }
}
