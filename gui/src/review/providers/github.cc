#include "github.hpp"

#include "../link_header.hpp"
#include "../log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace diffy::review {
namespace {

using json = nlohmann::json;

// --- small, null-safe JSON accessors (GitHub sends null for optional fields:
// a review comment's `line`/`start_line`, a PR's `merged_by`, etc.) ------------

const json&
jchild(const json& j, const char* key) {
    static const json kNull;
    if (j.is_object()) {
        auto it = j.find(key);
        if (it != j.end()) {
            return *it;
        }
    }
    return kNull;
}

std::string
jstr(const json& j, const char* key) {
    const json& v = jchild(j, key);
    return v.is_string() ? v.get<std::string>() : std::string{};
}

int
jint(const json& j, const char* key, int def = 0) {
    const json& v = jchild(j, key);
    return v.is_number_integer() ? v.get<int>() : def;
}

bool
jbool(const json& j, const char* key, bool def = false) {
    const json& v = jchild(j, key);
    return v.is_boolean() ? v.get<bool>() : def;
}

// An id field may arrive as a JSON number (users, PRs, comments) or string;
// normalize to a decimal string either way.
std::string
jid(const json& j, const char* key = "id") {
    const json& v = jchild(j, key);
    if (v.is_string()) {
        return v.get<std::string>();
    }
    if (v.is_number_unsigned()) {
        return std::to_string(v.get<std::uint64_t>());
    }
    if (v.is_number_integer()) {
        return std::to_string(v.get<std::int64_t>());
    }
    return {};
}

// --- transport -------------------------------------------------------------

std::optional<int>
retry_after(const HttpResponse& resp) {
    for (const auto& [n, v] : resp.headers) {
        std::string low;
        low.reserve(n.size());
        for (char ch : n) {
            low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (low == "retry-after") {
            try {
                return std::stoi(v);
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

// Pull a human-readable reason out of a GitHub error body
// ({"message":"…","errors":[{"message":"…"}]}), or fall back to a trimmed
// snippet, so a failed call shows *why* rather than a bare status code.
std::string
server_message(const std::string& body) {
    if (body.empty()) {
        return {};
    }
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_discarded()) {
        std::string m = jstr(j, "message");
        const json& errors = jchild(j, "errors");
        if (errors.is_array() && !errors.empty()) {
            const std::string detail = jstr(errors.front(), "message");
            if (!detail.empty()) {
                m = m.empty() ? detail : (m + ": " + detail);
            }
        }
        if (!m.empty()) {
            return m;
        }
    }
    return body.substr(0, 200);
}

// Map a transport failure or non-2xx status onto the normalized Error taxonomy;
// std::nullopt means the exchange succeeded (2xx). GitHub reports its primary
// rate limit as 403 with X-RateLimit-Remaining: 0 (and a Retry-After on secondary
// limits), so a 403 with the header exhausted is RateLimited, not Auth.
std::optional<Error>
http_error(const HttpResult& res) {
    if (!res.ok()) {
        const bool net = res.error == HttpError::Network || res.error == HttpError::Timeout ||
                         res.error == HttpError::Tls;
        return Error{net ? ErrorKind::Network : ErrorKind::Other, 0, res.message};
    }
    const long s = res.response.status;
    if (s >= 200 && s < 300) {
        return std::nullopt;
    }
    ErrorKind kind;
    auto header = [&](const char* name) {
        for (const auto& [n, v] : res.response.headers) {
            std::string low;
            for (char ch : n) {
                low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            if (low == name) {
                return v;
            }
        }
        return std::string{};
    };
    const bool rate_exhausted = header("x-ratelimit-remaining") == "0";
    if (s == 429 || (s == 403 && rate_exhausted)) {
        kind = ErrorKind::RateLimited;
    } else if (s == 401 || s == 403) {
        kind = ErrorKind::Auth;
    } else if (s == 404) {
        kind = ErrorKind::NotFound;
    } else if (s >= 500) {
        kind = ErrorKind::Network;
    } else {
        kind = ErrorKind::Other;
    }
    Error e{kind, static_cast<int>(s), "HTTP " + std::to_string(s)};
    const std::string sm = server_message(res.response.body);
    if (!sm.empty()) {
        e.message += ": " + sm;
    }
    if (kind == ErrorKind::RateLimited) {
        e.retry_after_secs = retry_after(res.response);
    }
    return e;
}

// Issue a request with GitHub's required headers: Bearer auth, a User-Agent
// (mandatory — the API 403s without it), the versioned Accept, and the API
// version pin. `accept` is overridable so file_at can ask for raw content.
HttpResult
send(HttpClient& http, const Credential& cred, const std::string& method, const std::string& url,
     const std::string& body, const std::string& accept = "application/vnd.github+json") {
    HttpRequest rq;
    rq.method = method;
    rq.url = url;
    // GitHub accepts a PAT/OAuth token as a Bearer; principal is unused.
    rq.headers.push_back({"Authorization", "Bearer " + cred.secret});
    rq.headers.push_back({"User-Agent", "diffy"});
    rq.headers.push_back({"Accept", accept});
    rq.headers.push_back({"X-GitHub-Api-Version", "2022-11-28"});
    if (!body.empty()) {
        rq.headers.push_back({"Content-Type", "application/json"});
        rq.body = body;
    }
    HttpResult res = http.send(rq);
    // URL + status only — never the Authorization header/token.
    log_line("[github] " + method + " " + url + " -> " +
             (res.ok() ? ("HTTP " + std::to_string(res.response.status))
                       : ("transport error: " + res.message)));
    return res;
}

Result<json>
req_json(HttpClient& http, const Credential& cred, const std::string& method,
         const std::string& url, const std::string& body = "") {
    const HttpResult res = send(http, cred, method, url, body);
    if (auto e = http_error(res)) {
        return Result<json>::err(*e);
    }
    if (res.response.body.empty()) {
        return Result<json>::ok(json::object());  // e.g. 204 No Content on delete
    }
    json j = json::parse(res.response.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        return Result<json>::err(
            {ErrorKind::Parse, static_cast<int>(res.response.status), "invalid JSON in response"});
    }
    return Result<json>::ok(std::move(j));
}

// Follow GitHub's RFC-5988 `Link:` header rel="next", concatenating each page's
// array body. Unlike Bitbucket (a body `next` URL), the cursor is in the header.
Result<std::vector<json>>
collect_all(HttpClient& http, const Credential& cred, std::string url) {
    std::vector<json> acc;
    for (int guard = 0; !url.empty() && guard < 100; ++guard) {
        const HttpResult res = send(http, cred, "GET", url, "");
        if (auto e = http_error(res)) {
            return Result<std::vector<json>>::err(*e);
        }
        json j = json::parse(res.response.body, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) {
            return Result<std::vector<json>>::err(
                {ErrorKind::Parse, static_cast<int>(res.response.status), "invalid JSON in response"});
        }
        if (j.is_array()) {
            for (auto& v : j) {
                acc.push_back(std::move(v));
            }
        }
        url = next_link(http.header(res.response, "Link"));
    }
    return Result<std::vector<json>>::ok(std::move(acc));
}

// --- JSON -> neutral model -------------------------------------------------

std::string
avatar_url(const json& user) {
    return jstr(user, "avatar_url");
}

Comment
to_comment(const json& j) {
    Comment cm;
    cm.id = jid(j);
    cm.parent_id = jid(j, "in_reply_to_id");
    const json& user = jchild(j, "user");
    cm.author = jstr(user, "login");
    cm.author_id = jid(user);  // numeric user id, as a string
    cm.author_avatar = avatar_url(user);
    cm.body_md = jstr(j, "body");
    cm.created = jstr(j, "created_at");
    return cm;
}

PullRequest
to_pr(const json& j) {
    PullRequest pr;
    pr.id = jid(j, "number");  // GitHub addresses a PR by its number
    pr.title = jstr(j, "title");
    pr.description = jstr(j, "body");
    const json& user = jchild(j, "user");
    pr.author = jstr(user, "login");
    pr.author_id = jid(user);
    pr.author_avatar = avatar_url(user);
    // Requested reviewers are those yet to review (approval isn't in the PR object
    // — it lives in /reviews); surface them as pending reviewers.
    const json& reviewers = jchild(j, "requested_reviewers");
    if (reviewers.is_array()) {
        for (const auto& u : reviewers) {
            Reviewer rv;
            rv.id = jid(u);
            rv.name = jstr(u, "login");
            rv.avatar = avatar_url(u);
            rv.approved = false;
            if (!rv.id.empty()) {
                pr.reviewers.push_back(std::move(rv));
            }
        }
    }
    pr.src_branch = jstr(jchild(j, "head"), "ref");
    pr.dst_branch = jstr(jchild(j, "base"), "ref");
    pr.draft = jbool(j, "draft");
    const std::string st = jstr(j, "state");  // "open" | "closed"
    pr.state = pr.draft                    ? ApprovalState::Draft
               : jbool(j, "merged")        ? ApprovalState::Merged
               : (st == "closed")          ? ApprovalState::Declined
                                           : ApprovalState::Open;
    pr.comment_count = jint(j, "comments") + jint(j, "review_comments");
    pr.updated = jstr(j, "updated_at");
    return pr;
}

PrFile
to_file(const json& j) {
    PrFile f;
    std::string status = jstr(j, "status");  // added/modified/removed/renamed/changed
    if (status == "removed") {
        status = "deleted";
    } else if (status == "changed") {
        status = "modified";
    }
    f.status = status;
    f.path = jstr(j, "filename");
    const std::string prev = jstr(j, "previous_filename");
    if (status == "renamed" && !prev.empty()) {
        f.old_path = prev;
    }
    f.additions = jint(j, "additions");
    f.deletions = jint(j, "deletions");
    return f;
}

PrCommit
to_commit(const json& j) {
    PrCommit c;
    c.sha = jstr(j, "sha");
    c.short_sha = c.sha.substr(0, std::min<std::size_t>(8, c.sha.size()));
    const json& commit = jchild(j, "commit");
    const std::string msg = jstr(commit, "message");
    c.summary = msg.substr(0, msg.find('\n'));
    c.message = msg;
    c.author = jstr(jchild(commit, "author"), "name");
    c.when = jstr(jchild(commit, "author"), "date");
    return c;
}

// GitHub's wire names for the merge methods it offers (no fast-forward — that
// falls back to a merge commit).
std::string
merge_method_wire(MergeStrategy s) {
    switch (s) {
        case MergeStrategy::Squash:
            return "squash";
        case MergeStrategy::Rebase:
            return "rebase";
        case MergeStrategy::MergeCommit:
        case MergeStrategy::FastForward:
        default:
            return "merge";
    }
}

// The review `event` for a verdict when submitting a batched review.
std::string
review_event(ApprovalState verdict) {
    switch (verdict) {
        case ApprovalState::Approved:
            return "APPROVE";
        case ApprovalState::ChangesRequested:
            return "REQUEST_CHANGES";
        default:
            return "COMMENT";
    }
}

// Build the JSON for one inline review comment (used by comment() and the batched
// submit_review()). side: New -> RIGHT, Old -> LEFT; a start line makes it a range.
json
inline_comment_json(const NewComment& nc) {
    json c;
    c["body"] = nc.body_md;
    c["path"] = nc.anchor.new_path;
    c["side"] = nc.anchor.side == Side::New ? "RIGHT" : "LEFT";
    c["line"] = nc.anchor.end.line != 0 ? nc.anchor.end.line : nc.anchor.start.line;
    if (nc.anchor.end.line != 0 && nc.anchor.start.line != nc.anchor.end.line) {
        c["start_line"] = nc.anchor.start.line;
        c["start_side"] = nc.anchor.side == Side::New ? "RIGHT" : "LEFT";
    }
    return c;
}

// Group a flat GitHub review-comment list into anchored threads: each reply
// carries in_reply_to_id pointing at the thread's root, so we bucket by
// root-or-self and anchor from the root's path/line/side.
std::vector<ReviewThread>
parse_review_threads(const std::vector<json>& comments) {
    std::unordered_map<std::string, ReviewThread> threads;
    std::vector<std::string> root_order;
    for (const auto& c : comments) {
        const std::string cid = jid(c);
        if (cid.empty()) {
            continue;
        }
        const std::string reply_to = jid(c, "in_reply_to_id");
        const std::string root = reply_to.empty() ? cid : reply_to;
        ReviewThread& th = threads[root];
        if (th.id.empty()) {
            th.id = root;
            root_order.push_back(root);
        }
        // Anchor from whichever comment is the root (its own path/line/side).
        if (cid == root) {
            RangeAnchor a;
            a.new_path = jstr(c, "path");
            a.side = jstr(c, "side") == "LEFT" ? Side::Old : Side::New;
            // `line` is null when the comment is outdated (no longer maps onto the
            // latest diff); fall back to original_line and flag it.
            const json& line = jchild(c, "line");
            if (line.is_number_integer()) {
                a.end.line = line.get<int>();
            } else {
                a.end.line = jint(c, "original_line");
                th.outdated = true;
            }
            const json& start = jchild(c, "start_line");
            a.start.line = start.is_number_integer() ? start.get<int>() : a.end.line;
            th.anchor = a;
        }
        th.comments.push_back(to_comment(c));
    }
    std::vector<ReviewThread> out;
    out.reserve(root_order.size());
    for (const std::string& root : root_order) {
        out.push_back(std::move(threads[root]));
    }
    return out;
}

}  // namespace

// --- GitHubClient -----------------------------------------------------------

GitHubClient::GitHubClient(HttpClient& http, Credential cred, std::string owner, std::string repo,
                           std::string base_url)
    : http_(http),
      cred_(std::move(cred)),
      owner_(std::move(owner)),
      repo_(std::move(repo)),
      base_(std::move(base_url)) {}

std::string
GitHubClient::repo_url() const {
    return base_ + "/repos/" + owner_ + "/" + repo_;
}

std::string
GitHubClient::pr_url(const std::string& id) const {
    return repo_url() + "/pulls/" + id;
}

Capabilities
GitHubClient::capabilities() const {
    Capabilities c;
    c.item_noun = "pull request";
    c.granularity = CommentGranularity::LineRange;  // inline comments keep a line range
    c.pending_review_batch = true;                  // comments accumulate; submit_review() posts
    c.request_changes_state = true;                 // an explicit changes-requested review state
    c.suggestions = true;                           // ```suggestion blocks
    c.thread_resolution = false;                    // resolving threads needs GraphQL, not REST v3
    c.commit_comments = true;                       // /commits/{sha}/comments (GET + POST)
    c.draft_items = true;
    c.merge_strategies = {MergeStrategy::MergeCommit, MergeStrategy::Squash, MergeStrategy::Rebase};
    return c;
}

Result<Account>
GitHubClient::whoami() {
    Result<json> r = req_json(http_, cred_, "GET", base_ + "/user");
    if (!r) {
        return Result<Account>::err(r.error());
    }
    const json& j = r.value();
    Account a;
    a.id = jid(j);
    a.username = jstr(j, "login");
    a.display_name = jstr(j, "name");
    if (a.display_name.empty()) {
        a.display_name = a.username;
    }
    return Result<Account>::ok(a);
}

Result<Page<PullRequest>>
GitHubClient::list_open(const std::string& cursor) {
    // First page builds the URL; a non-empty cursor IS the next page's URL (the
    // rel="next" Link), so it's requested verbatim.
    const std::string url =
        cursor.empty() ? repo_url() + "/pulls?state=open&per_page=50&sort=updated&direction=desc"
                        : cursor;
    const HttpResult res = send(http_, cred_, "GET", url, "");
    if (auto e = http_error(res)) {
        return Result<Page<PullRequest>>::err(*e);
    }
    json j = json::parse(res.response.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        return Result<Page<PullRequest>>::err(
            {ErrorKind::Parse, static_cast<int>(res.response.status), "invalid JSON in response"});
    }
    Page<PullRequest> pg;
    if (j.is_array()) {
        for (const auto& v : j) {
            pg.items.push_back(to_pr(v));
        }
    }
    pg.next_cursor = next_link(http_.header(res.response, "Link"));
    pg.has_more = !pg.next_cursor.empty();
    return Result<Page<PullRequest>>::ok(std::move(pg));
}

Result<PullRequest>
GitHubClient::get(const std::string& id) {
    Result<json> r = req_json(http_, cred_, "GET", pr_url(id));
    if (!r) {
        return Result<PullRequest>::err(r.error());
    }
    return Result<PullRequest>::ok(to_pr(r.value()));
}

Result<PrRefs>
GitHubClient::refs(const std::string& id) {
    Result<json> r = req_json(http_, cred_, "GET", pr_url(id));
    if (!r) {
        return Result<PrRefs>::err(r.error());
    }
    const json& j = r.value();
    PrRefs refs;
    refs.head_sha = jstr(jchild(j, "head"), "sha");
    refs.base_sha = jstr(jchild(j, "base"), "sha");
    refs.base_ref = jstr(jchild(j, "base"), "ref");
    refs.head_ref = "refs/pull/" + id + "/head";  // fetchable PR head ref
    return Result<PrRefs>::ok(refs);
}

Result<std::vector<PrFile>>
GitHubClient::files(const std::string& id) {
    Result<std::vector<json>> r = collect_all(http_, cred_, pr_url(id) + "/files?per_page=100");
    if (!r) {
        return Result<std::vector<PrFile>>::err(r.error());
    }
    std::vector<PrFile> out;
    out.reserve(r.value().size());
    for (const auto& v : r.value()) {
        out.push_back(to_file(v));
    }
    return Result<std::vector<PrFile>>::ok(std::move(out));
}

Result<std::vector<PrCommit>>
GitHubClient::commits(const std::string& id) {
    Result<std::vector<json>> r = collect_all(http_, cred_, pr_url(id) + "/commits?per_page=100");
    if (!r) {
        return Result<std::vector<PrCommit>>::err(r.error());
    }
    std::vector<PrCommit> out;
    out.reserve(r.value().size());
    for (const auto& v : r.value()) {
        out.push_back(to_commit(v));
    }
    return Result<std::vector<PrCommit>>::ok(std::move(out));
}

Result<std::vector<ReviewThread>>
GitHubClient::threads(const std::string& id) {
    // Inline review comments (anchored) + general PR comments (the issues endpoint).
    Result<std::vector<json>> review = collect_all(http_, cred_, pr_url(id) + "/comments?per_page=100");
    if (!review) {
        return Result<std::vector<ReviewThread>>::err(review.error());
    }
    std::vector<ReviewThread> out = parse_review_threads(review.value());

    Result<std::vector<json>> general =
        collect_all(http_, cred_, repo_url() + "/issues/" + id + "/comments?per_page=100");
    if (general) {
        for (const auto& c : general.value()) {
            ReviewThread th;
            th.id = jid(c);
            th.comments.push_back(to_comment(c));  // no anchor -> a general thread
            out.push_back(std::move(th));
        }
    }
    return Result<std::vector<ReviewThread>>::ok(std::move(out));
}

Result<std::vector<ReviewThread>>
GitHubClient::commit_threads(const std::string& sha) {
    Result<std::vector<json>> r =
        collect_all(http_, cred_, repo_url() + "/commits/" + sha + "/comments?per_page=100");
    if (!r) {
        return Result<std::vector<ReviewThread>>::err(r.error());
    }
    // Commit comments aren't threaded; each is its own single-comment thread,
    // anchored by path + line where present.
    std::vector<ReviewThread> out;
    for (const auto& c : r.value()) {
        ReviewThread th;
        th.id = jid(c);
        const std::string path = jstr(c, "path");
        if (!path.empty()) {
            RangeAnchor a;
            a.new_path = path;
            a.side = Side::New;
            a.start.line = jint(c, "line");
            a.end = a.start;
            th.anchor = a;
        }
        th.comments.push_back(to_comment(c));
        out.push_back(std::move(th));
    }
    return Result<std::vector<ReviewThread>>::ok(std::move(out));
}

Result<std::string>
GitHubClient::file_at(const std::string& sha, const std::string& path) {
    // Ask the contents API for the raw blob (Accept: raw) so we don't have to
    // base64-decode the JSON `content` field.
    const std::string url = repo_url() + "/contents/" + path + "?ref=" + sha;
    const HttpResult res = send(http_, cred_, "GET", url, "", "application/vnd.github.raw");
    if (auto e = http_error(res)) {
        return Result<std::string>::err(*e);
    }
    return Result<std::string>::ok(res.response.body);
}

Result<Comment>
GitHubClient::comment(const std::string& id, const NewComment& nc) {
    // A reply, a whole-PR (general) comment, or a fresh inline comment. Each posts
    // immediately as a standalone review; the pending-batch path is submit_review().
    if (nc.reply_to) {
        json body;
        body["body"] = nc.body_md;
        try {
            body["in_reply_to"] = static_cast<std::int64_t>(std::stoll(*nc.reply_to));
        } catch (...) {
            return Result<Comment>::err({ErrorKind::Other, 0, "invalid reply_to comment id"});
        }
        Result<json> r = req_json(http_, cred_, "POST", pr_url(id) + "/comments", body.dump());
        return r ? Result<Comment>::ok(to_comment(r.value())) : Result<Comment>::err(r.error());
    }
    if (nc.general || nc.anchor.new_path.empty()) {
        json body;
        body["body"] = nc.body_md;
        Result<json> r =
            req_json(http_, cred_, "POST", repo_url() + "/issues/" + id + "/comments", body.dump());
        return r ? Result<Comment>::ok(to_comment(r.value())) : Result<Comment>::err(r.error());
    }
    json body = inline_comment_json(nc);
    // GitHub anchors a new inline comment to a specific commit (the PR head).
    if (!nc.anchor.ctx.head_sha.empty()) {
        body["commit_id"] = nc.anchor.ctx.head_sha;
    }
    Result<json> r = req_json(http_, cred_, "POST", pr_url(id) + "/comments", body.dump());
    return r ? Result<Comment>::ok(to_comment(r.value())) : Result<Comment>::err(r.error());
}

Result<Comment>
GitHubClient::comment_on_commit(const std::string& sha, const NewComment& nc) {
    json body;
    body["body"] = nc.body_md;
    if (!nc.general && !nc.anchor.new_path.empty()) {
        body["path"] = nc.anchor.new_path;
        body["line"] = nc.anchor.start.line;
    }
    Result<json> r =
        req_json(http_, cred_, "POST", repo_url() + "/commits/" + sha + "/comments", body.dump());
    return r ? Result<Comment>::ok(to_comment(r.value())) : Result<Comment>::err(r.error());
}

Result<Comment>
GitHubClient::edit_comment(const std::string& /*id*/, const std::string& comment_id,
                           const std::string& body_md) {
    // Review comments are edited by their global id, not under the PR number.
    json body;
    body["body"] = body_md;
    Result<json> r =
        req_json(http_, cred_, "PATCH", repo_url() + "/pulls/comments/" + comment_id, body.dump());
    return r ? Result<Comment>::ok(to_comment(r.value())) : Result<Comment>::err(r.error());
}

Result<void>
GitHubClient::delete_comment(const std::string& /*id*/, const std::string& comment_id) {
    const HttpResult res =
        send(http_, cred_, "DELETE", repo_url() + "/pulls/comments/" + comment_id, "");
    if (auto e = http_error(res)) {
        return Result<void>::err(*e);
    }
    return Result<void>::ok();
}

Result<void>
GitHubClient::approve(const std::string& id) {
    Result<json> r = req_json(http_, cred_, "POST", pr_url(id) + "/reviews", R"({"event":"APPROVE"})");
    if (!r) {
        return Result<void>::err(r.error());
    }
    return Result<void>::ok();
}

Result<void>
GitHubClient::unapprove(const std::string& id) {
    // GitHub has no direct unapprove: dismiss the caller's own latest APPROVED
    // review. Identify "mine" via whoami, then PUT its dismissal. The seam holds —
    // unapprove(id) still expresses it — but it costs extra round-trips.
    Result<Account> me = whoami();
    if (!me) {
        return Result<void>::err(me.error());
    }
    Result<std::vector<json>> reviews = collect_all(http_, cred_, pr_url(id) + "/reviews?per_page=100");
    if (!reviews) {
        return Result<void>::err(reviews.error());
    }
    std::string review_id;
    for (const auto& rv : reviews.value()) {
        if (jstr(rv, "state") == "APPROVED" && jid(jchild(rv, "user")) == me.value().id) {
            review_id = jid(rv);  // keep the last one (reviews come oldest-first)
        }
    }
    if (review_id.empty()) {
        return Result<void>::ok();  // nothing of ours to dismiss
    }
    const HttpResult res = send(http_, cred_, "PUT",
                                pr_url(id) + "/reviews/" + review_id + "/dismissals",
                                R"({"message":"Approval dismissed."})");
    if (auto e = http_error(res)) {
        return Result<void>::err(*e);
    }
    return Result<void>::ok();
}

Result<void>
GitHubClient::request_changes(const std::string& id) {
    // REQUEST_CHANGES requires a body; a request-changes with no note gets a stub.
    Result<json> r = req_json(http_, cred_, "POST", pr_url(id) + "/reviews",
                              R"({"event":"REQUEST_CHANGES","body":"Changes requested."})");
    if (!r) {
        return Result<void>::err(r.error());
    }
    return Result<void>::ok();
}

Result<void>
GitHubClient::submit_review(const std::string& id, const Review& review) {
    // A batched review: an overall body + verdict plus the pending inline comments,
    // posted atomically as one GitHub review.
    json body;
    body["event"] = review_event(review.verdict);
    if (!review.body_md.empty()) {
        body["body"] = review.body_md;
    }
    json comments = json::array();
    for (const NewComment& nc : review.comments) {
        if (!nc.general && !nc.anchor.new_path.empty()) {
            comments.push_back(inline_comment_json(nc));
        }
    }
    if (!comments.empty()) {
        body["comments"] = std::move(comments);
    }
    Result<json> r = req_json(http_, cred_, "POST", pr_url(id) + "/reviews", body.dump());
    if (!r) {
        return Result<void>::err(r.error());
    }
    return Result<void>::ok();
}

Result<void>
GitHubClient::merge(const std::string& id, MergeStrategy strategy, const std::string& message) {
    json body;
    body["merge_method"] = merge_method_wire(strategy);
    if (!message.empty()) {
        body["commit_title"] = message;
    }
    const HttpResult res = send(http_, cred_, "PUT", pr_url(id) + "/merge", body.dump());
    if (auto e = http_error(res)) {
        return Result<void>::err(*e);
    }
    return Result<void>::ok();
}

ProviderPlugin
github_plugin() {
    ProviderPlugin p;
    p.id = "github";
    p.matches = [](const RemoteUrl& u) { return u.host.find("github.com") != std::string::npos; };
    p.make = [](const RemoteUrl& u, const RemoteConfig& cfg, HttpClient& http,
                const Credential& cred) -> std::unique_ptr<ReviewProvider> {
        const std::string base = cfg.base_url.empty() ? "https://api.github.com" : cfg.base_url;
        return std::make_unique<GitHubClient>(http, cred, u.owner, u.repo, base);
    };
    p.auth.methods = {AuthMethod::Bearer};
    p.auth.scopes = {"repo"};
    p.auth.help_text =
        "Create a personal access token (github.com → Settings → Developer settings → "
        "Personal access tokens) with the 'repo' scope, and paste it as the token. "
        "Leave the username blank.";
    return p;
}

}  // namespace diffy::review
