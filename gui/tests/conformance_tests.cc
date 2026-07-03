// Proves the conformance battery + MockHttpClient work end-to-end without any
// real provider or network. A hand-written FakeProvider returns hardcoded,
// internally-consistent neutral data; run_conformance() must pass against it.
// A deliberately-broken variant must make run_conformance() report failures, and
// a MockHttpClient smoke test proves canned responses replay and unmatched
// requests fail loudly. Concrete provider tests (#27/#28) replace FakeProvider
// with a real provider over a MockHttpClient wired with recorded fixtures.

#include "review/capabilities.hpp"
#include "review/conformance.hpp"
#include "review/http_client.hpp"
#include "review/mock_http_client.hpp"
#include "review/model.hpp"
#include "review/result.hpp"
#include "review/review_provider.hpp"

#include <doctest.h>

#include <string>
#include <vector>

using namespace diffy::review;

namespace {

// A minimal, internally-consistent provider: a couple of open PRs, a get() that
// echoes the id, non-empty refs, one file, one inline thread with a valid
// RangeAnchor. capabilities() advertises request_changes_state == false and
// pending_review_batch == false, so the gated writes must return Unsupported.
struct FakeProvider : ReviewProvider {
    Capabilities
    capabilities() const override {
        Capabilities caps;
        caps.item_noun = "pull request";
        caps.granularity = CommentGranularity::LineRange;
        caps.request_changes_state = false;
        caps.pending_review_batch = false;
        return caps;
    }

    Result<Account>
    whoami() override {
        Account a;
        a.id = "acct-1";
        a.username = "reviewer";
        a.display_name = "The Reviewer";
        return Result<Account>::ok(a);
    }

    Result<Page<PullRequest>>
    list_open(const std::string& /*cursor*/ = "") override {
        Page<PullRequest> page;
        page.items.push_back(make_pr("PR-1"));
        page.items.push_back(make_pr("PR-2"));
        page.has_more = false;
        return Result<Page<PullRequest>>::ok(page);
    }

    Result<PullRequest>
    get(const std::string& id) override {
        return Result<PullRequest>::ok(make_pr(id));
    }

    Result<PrRefs>
    refs(const std::string& /*id*/) override {
        PrRefs r;
        r.head_ref = "feature/x";
        r.base_ref = "master";
        r.head_sha = "1111111111111111111111111111111111111111";
        r.base_sha = "2222222222222222222222222222222222222222";
        return Result<PrRefs>::ok(r);
    }

    Result<std::vector<PrFile>>
    files(const std::string& /*id*/) override {
        PrFile f;
        f.path = "src/main.cpp";
        f.status = "modified";
        f.additions = 10;
        f.deletions = 3;
        return Result<std::vector<PrFile>>::ok({f});
    }

    Result<std::vector<PrCommit>>
    commits(const std::string& /*id*/) override {
        PrCommit c;
        c.sha = "1111111111111111111111111111111111111111";
        c.short_sha = "1111111";
        c.summary = "Do the thing";
        c.author = "reviewer";
        c.when = "2026-06-29T10:00:00Z";
        return Result<std::vector<PrCommit>>::ok({c});
    }

    Result<std::vector<ReviewThread>>
    threads(const std::string& /*id*/) override {
        ReviewThread t;
        t.id = "thread-1";
        t.anchor.new_path = "src/main.cpp";
        t.anchor.side = Side::New;
        t.anchor.start.line = 12;
        t.anchor.end.line = 14;
        Comment comment;
        comment.id = "c-1";
        comment.author = "reviewer";
        comment.body_md = "Consider extracting this.";
        comment.created = "2026-06-29T10:05:00Z";
        t.comments.push_back(comment);
        return Result<std::vector<ReviewThread>>::ok({t});
    }

    Result<std::string>
    file_at(const std::string& /*sha*/, const std::string& /*path*/) override {
        return Result<std::string>::ok("file contents\n");
    }

    Result<Comment>
    comment(const std::string& /*id*/, const NewComment&) override {
        Comment c;
        c.id = "c-new";
        c.author = "reviewer";
        c.body_md = "posted";
        c.created = "2026-06-29T10:06:00Z";
        return Result<Comment>::ok(c);
    }

    Result<std::vector<ReviewThread>>
    commit_threads(const std::string& /*sha*/) override {
        return Result<std::vector<ReviewThread>>::ok({});
    }

    Result<Comment>
    comment_on_commit(const std::string& /*sha*/, const NewComment&) override {
        Comment c;
        c.id = "cc-new";
        c.author = "reviewer";
        c.body_md = "posted on commit";
        c.created = "2026-06-29T10:07:00Z";
        return Result<Comment>::ok(c);
    }

    Result<Comment>
    edit_comment(const std::string& /*id*/, const std::string& comment_id,
                 const std::string& body_md) override {
        Comment c;
        c.id = comment_id;
        c.author = "reviewer";
        c.body_md = body_md;
        c.created = "2026-06-29T10:08:00Z";
        return Result<Comment>::ok(c);
    }

    Result<void>
    delete_comment(const std::string& /*id*/, const std::string& /*comment_id*/) override {
        return Result<void>::ok();
    }

    Result<void>
    approve(const std::string& /*id*/) override {
        return Result<void>::ok();
    }

    Result<void>
    unapprove(const std::string& /*id*/) override {
        return Result<void>::ok();
    }

    Result<void>
    request_changes(const std::string& /*id*/) override {
        // Gated: capabilities().request_changes_state == false.
        return Result<void>::err(Error{ErrorKind::Unsupported, 0, "request changes not supported", {}});
    }

    Result<void>
    submit_review(const std::string& /*id*/, const Review&) override {
        // Gated: capabilities().pending_review_batch == false.
        return Result<void>::err(Error{ErrorKind::Unsupported, 0, "batched review not supported", {}});
    }

    Result<void>
    merge(const std::string& /*id*/, MergeStrategy /*strategy*/,
          const std::string& /*message*/ = "") override {
        return Result<void>::ok();
    }

  protected:
    static PullRequest
    make_pr(const std::string& id) {
        PullRequest pr;
        pr.id = id;
        pr.title = "Title for " + id;
        pr.author = "reviewer";
        pr.src_branch = "feature/x";
        pr.dst_branch = "master";
        pr.state = ApprovalState::Open;
        pr.updated = "2026-06-29T09:00:00Z";
        return pr;
    }
};

// Same as FakeProvider but get() echoes the WRONG id, so the get() invariant
// must fail.
struct BrokenProvider : FakeProvider {
    Result<PullRequest>
    get(const std::string& /*id*/) override {
        return Result<PullRequest>::ok(make_pr("WRONG-ID"));
    }
};

}  // namespace

TEST_CASE("run_conformance passes against a well-formed FakeProvider") {
    FakeProvider fake;
    ConformanceReport report = run_conformance(fake, "PR-1");

    // If this fails, print which invariants tripped.
    for (const std::string& f : report.failures) {
        INFO("conformance failure: ", f);
    }
    CHECK(report.ok());
    CHECK(report.failed == 0);
    CHECK(report.passed > 0);
}

TEST_CASE("run_conformance reports failures against a broken provider") {
    BrokenProvider broken;
    ConformanceReport report = run_conformance(broken, "PR-1");

    CHECK_FALSE(report.ok());
    CHECK(report.failed > 0);
}

TEST_CASE("MockHttpClient replays canned responses and records requests") {
    MockHttpClient http;

    HttpResponse resp;
    resp.status = 200;
    resp.headers.push_back({"Content-Type", "application/json"});
    resp.body = R"({"id":"PR-1"})";
    http.on("GET", "/pullrequests/PR-1", resp);

    HttpRequest req;
    req.method = "GET";
    req.url = "https://api.example.com/repos/x/pullrequests/PR-1";
    HttpResult r = http.send(req);

    CHECK(r.ok());
    CHECK(r.response.status == 200);
    CHECK(r.response.body == R"({"id":"PR-1"})");
    CHECK(http.requested("/pullrequests/PR-1"));
    CHECK(http.sent.size() == 1);
    CHECK(http.sent.front().method == "GET");
}

TEST_CASE("MockHttpClient fails loudly on an unmatched request") {
    MockHttpClient http;

    HttpRequest req;
    req.method = "POST";
    req.url = "https://api.example.com/nope";
    HttpResult r = http.send(req);

    CHECK_FALSE(r.ok());
    CHECK(r.error == HttpError::Internal);
    CHECK_FALSE(r.message.empty());
    CHECK_FALSE(http.requested("/pullrequests/PR-1"));
    CHECK(http.requested("/nope"));
}

TEST_CASE("MockHttpClient can queue a network error") {
    MockHttpClient http;
    http.on_error("GET", "/flaky", HttpError::Network, "connection reset");

    HttpRequest req;
    req.url = "https://api.example.com/flaky";
    HttpResult r = http.send(req);

    CHECK_FALSE(r.ok());
    CHECK(r.error == HttpError::Network);
    CHECK(r.message == "connection reset");
}
