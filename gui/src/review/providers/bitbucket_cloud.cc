#include "bitbucket_cloud.hpp"

#include "../log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace diffy::review {
namespace {

using json = nlohmann::json;

// --- small, null-safe JSON accessors ---------------------------------------
// Bitbucket routinely sends null for optional objects (e.g. a renamed file's
// `old`), so every accessor guards type before reading and falls back to a
// default rather than throwing.

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

// An id field may arrive as a JSON number (PRs, comments) or string; normalize to
// a decimal string either way.
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

// --- auth + transport ------------------------------------------------------

std::string
base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        std::uint32_t n = (static_cast<unsigned char>(in[i]) << 16) |
                          (static_cast<unsigned char>(in[i + 1]) << 8) |
                          static_cast<unsigned char>(in[i + 2]);
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(T[(n >> 6) & 63]);
        out.push_back(T[n & 63]);
    }
    if (i < in.size()) {
        std::uint32_t n = static_cast<unsigned char>(in[i]) << 16;
        if (i + 1 < in.size()) {
            n |= static_cast<unsigned char>(in[i + 1]) << 8;
        }
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(i + 1 < in.size() ? T[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

void
add_auth(HttpRequest& rq, const Credential& c) {
    if (c.method == AuthMethod::BasicToken) {
        rq.headers.push_back({"Authorization", "Basic " + base64(c.principal + ":" + c.secret)});
    } else {
        rq.headers.push_back({"Authorization", "Bearer " + c.secret});
    }
}

std::optional<int>
retry_after(const HttpResponse& resp) {
    for (const auto& [n, v] : resp.headers) {
        if (n.size() == 11) {
            std::string low;
            low.reserve(11);
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
    }
    return std::nullopt;
}

// Pull a human-readable reason out of a Bitbucket error body
// ({"type":"error","error":{"message":"…"}}), or fall back to a trimmed snippet,
// so a failed connect shows *why* rather than a bare status code.
std::string
server_message(const std::string& body) {
    if (body.empty()) {
        return {};
    }
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_discarded()) {
        const json& err = jchild(j, "error");
        std::string m = jstr(err, "message");
        if (m.empty()) {
            m = jstr(err, "detail");
        }
        if (m.empty()) {
            m = jstr(j, "error_description");  // OAuth-style bodies
        }
        if (!m.empty()) {
            return m;
        }
    }
    std::string snippet = body.substr(0, 200);
    return snippet;
}

// Map a transport failure or non-2xx status onto the normalized Error taxonomy;
// std::nullopt means the exchange succeeded (2xx).
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
    if (s == 401 || s == 403) {
        kind = ErrorKind::Auth;
    } else if (s == 404) {
        kind = ErrorKind::NotFound;
    } else if (s == 429) {
        kind = ErrorKind::RateLimited;
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

HttpResult
send(HttpClient& http, const Credential& cred, const std::string& method, const std::string& url,
     const std::string& body) {
    HttpRequest rq;
    rq.method = method;
    rq.url = url;
    add_auth(rq, cred);
    rq.headers.push_back({"Accept", "application/json"});
    if (!body.empty()) {
        rq.headers.push_back({"Content-Type", "application/json"});
        rq.body = body;
    }
    HttpResult res = http.send(rq);
    // URL + status only — never the Authorization header/token.
    log_line("[bitbucket] " + method + " " + url + " -> " +
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
    json j = json::parse(res.response.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        return Result<json>::err(
            {ErrorKind::Parse, static_cast<int>(res.response.status), "invalid JSON in response"});
    }
    return Result<json>::ok(std::move(j));
}

// Follow Bitbucket's `next` links, concatenating every page's `values`.
Result<std::vector<json>>
collect_all(HttpClient& http, const Credential& cred, std::string url) {
    std::vector<json> acc;
    for (int guard = 0; !url.empty() && guard < 100; ++guard) {
        Result<json> page = req_json(http, cred, "GET", url);
        if (!page) {
            return Result<std::vector<json>>::err(page.error());
        }
        const json& values = jchild(page.value(), "values");
        if (values.is_array()) {
            for (const auto& v : values) {
                acc.push_back(v);
            }
        }
        url = jstr(page.value(), "next");
    }
    return Result<std::vector<json>>::ok(std::move(acc));
}

// --- JSON -> neutral model -------------------------------------------------

// Turn Bitbucket's rendered comment HTML into plain text. @mentions resolve to
// display names in `html`, whereas `raw` only has @{account-id}. Strips tags and
// decodes the common entities, collapsing whitespace.
std::string
strip_html(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (char ch : html) {
        if (ch == '<') {
            in_tag = true;
        } else if (ch == '>') {
            in_tag = false;
            out.push_back(' ');
        } else if (!in_tag) {
            out.push_back(ch);
        }
    }
    auto rep = [&](const char* from, const char* to) {
        const std::string f = from, t = to;
        std::size_t p = 0;
        while ((p = out.find(f, p)) != std::string::npos) {
            out.replace(p, f.size(), t);
            p += t.size();
        }
    };
    rep("&lt;", "<");
    rep("&gt;", ">");
    rep("&quot;", "\"");
    rep("&#39;", "'");
    rep("&#x27;", "'");
    rep("&nbsp;", " ");
    rep("&amp;", "&");
    std::string cleaned;
    cleaned.reserve(out.size());
    bool pending_space = false;
    for (char ch : out) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            pending_space = !cleaned.empty();
        } else {
            if (pending_space) {
                cleaned.push_back(' ');
                pending_space = false;
            }
            cleaned.push_back(ch);
        }
    }
    return cleaned;
}

// A comment's display text: rendered HTML (mentions as names) if present, else raw.
std::string
comment_body(const json& content) {
    const std::string html = jstr(content, "html");
    return html.empty() ? jstr(content, "raw") : strip_html(html);
}

PullRequest
to_pr(const json& j) {
    PullRequest pr;
    pr.id = jid(j);
    pr.title = jstr(j, "title");
    pr.description = jstr(j, "description");
    if (pr.description.empty()) {
        pr.description = jstr(jchild(j, "summary"), "raw");
    }
    const json& author = jchild(j, "author");
    pr.author = jstr(author, "display_name");
    pr.author_id = jstr(author, "account_id");
    if (pr.author_id.empty()) {
        pr.author_id = jstr(author, "uuid");
    }
    const json& participants = jchild(j, "participants");
    if (participants.is_array()) {
        for (const auto& p : participants) {
            if (jstr(p, "role") != "REVIEWER") {
                continue;
            }
            const json& u = jchild(p, "user");
            std::string uid = jstr(u, "account_id");
            if (uid.empty()) {
                uid = jstr(u, "uuid");
            }
            if (!uid.empty()) {
                pr.reviewers.push_back(Reviewer{uid, jbool(p, "approved")});
            }
        }
    }
    pr.src_branch = jstr(jchild(jchild(j, "source"), "branch"), "name");
    pr.dst_branch = jstr(jchild(jchild(j, "destination"), "branch"), "name");
    pr.draft = jbool(j, "draft");
    const std::string st = jstr(j, "state");
    pr.state = pr.draft                                      ? ApprovalState::Draft
               : st == "MERGED"                              ? ApprovalState::Merged
               : (st == "DECLINED" || st == "SUPERSEDED")    ? ApprovalState::Declined
                                                             : ApprovalState::Open;
    pr.comment_count = jint(j, "comment_count");
    pr.updated = jstr(j, "updated_on");
    return pr;
}

PrRefs
to_refs(const json& j, const std::string& id) {
    PrRefs r;
    r.head_sha = jstr(jchild(jchild(j, "source"), "commit"), "hash");
    r.base_sha = jstr(jchild(jchild(j, "destination"), "commit"), "hash");
    r.base_ref = jstr(jchild(jchild(j, "destination"), "branch"), "name");
    r.head_ref = "refs/pull-requests/" + id + "/from";
    return r;
}

PrFile
to_file(const json& j) {
    PrFile f;
    std::string status = jstr(j, "status");  // added/modified/removed/renamed
    if (status == "removed") {
        status = "deleted";
    }
    f.status = status;
    const std::string np = jstr(jchild(j, "new"), "path");
    const std::string op = jstr(jchild(j, "old"), "path");
    f.path = !np.empty() ? np : op;
    if (status == "renamed" && !op.empty()) {
        f.old_path = op;
    }
    f.additions = jint(j, "lines_added");
    f.deletions = jint(j, "lines_removed");
    return f;
}

PrCommit
to_commit(const json& j) {
    PrCommit c;
    c.sha = jstr(j, "hash");
    c.short_sha = c.sha.substr(0, std::min<std::size_t>(8, c.sha.size()));
    const std::string msg = jstr(j, "message");
    c.summary = msg.substr(0, msg.find('\n'));
    const json& au = jchild(j, "author");
    c.author = jstr(jchild(au, "user"), "display_name");
    if (c.author.empty()) {
        c.author = jstr(au, "raw");
    }
    c.when = jstr(j, "date");
    return c;
}

}  // namespace

// --- BitbucketCloudClient ---------------------------------------------------

BitbucketCloudClient::BitbucketCloudClient(HttpClient& http, Credential cred, std::string workspace,
                                           std::string repo_slug, std::string base_url)
    : http_(http),
      cred_(std::move(cred)),
      ws_(std::move(workspace)),
      repo_(std::move(repo_slug)),
      base_(std::move(base_url)) {}

std::string
BitbucketCloudClient::pr_url(const std::string& id) const {
    return base_ + "/repositories/" + ws_ + "/" + repo_ + "/pullrequests/" + id;
}

std::string
BitbucketCloudClient::commit_url(const std::string& sha) const {
    return base_ + "/repositories/" + ws_ + "/" + repo_ + "/commit/" + sha;
}

Capabilities
BitbucketCloudClient::capabilities() const {
    Capabilities c;
    c.item_noun = "pull request";
    c.granularity = CommentGranularity::Line;  // Bitbucket Cloud inline comments are single-line
    c.pending_review_batch = false;            // comments post immediately
    c.request_changes_state = false;           // no explicit "changes requested" state
    c.suggestions = false;
    c.thread_resolution = true;
    c.draft_items = true;
    c.commit_comments = true;  // /commit/{sha}/comments (GET + POST)
    c.merge_strategies = {MergeStrategy::MergeCommit, MergeStrategy::Squash,
                          MergeStrategy::FastForward};
    return c;
}

Result<Account>
BitbucketCloudClient::whoami() {
    Result<json> r = req_json(http_, cred_, "GET", base_ + "/user");
    if (!r) {
        return Result<Account>::err(r.error());
    }
    const json& j = r.value();
    Account a;
    a.id = jstr(j, "account_id");
    if (a.id.empty()) {
        a.id = jstr(j, "uuid");
    }
    a.username = jstr(j, "nickname");
    if (a.username.empty()) {
        a.username = jstr(j, "username");
    }
    a.display_name = jstr(j, "display_name");
    return Result<Account>::ok(a);
}

Result<Page<PullRequest>>
BitbucketCloudClient::list_open(const std::string& cursor) {
    // Request participants alongside the default fields so the UI can group PRs by
    // "needs your review" (fields=+values.participants; '+' encoded as %2B).
    const std::string url =
        cursor.empty() ? base_ + "/repositories/" + ws_ + "/" + repo_ +
                             "/pullrequests?state=OPEN&fields=%2Bvalues.participants"
                       : cursor;
    Result<json> r = req_json(http_, cred_, "GET", url);
    if (!r) {
        return Result<Page<PullRequest>>::err(r.error());
    }
    const json& j = r.value();
    Page<PullRequest> pg;
    const json& values = jchild(j, "values");
    if (values.is_array()) {
        for (const auto& v : values) {
            pg.items.push_back(to_pr(v));
        }
    }
    pg.next_cursor = jstr(j, "next");
    pg.has_more = !pg.next_cursor.empty();
    return Result<Page<PullRequest>>::ok(std::move(pg));
}

Result<PullRequest>
BitbucketCloudClient::get(const std::string& id) {
    Result<json> r = req_json(http_, cred_, "GET", pr_url(id));
    if (!r) {
        return Result<PullRequest>::err(r.error());
    }
    return Result<PullRequest>::ok(to_pr(r.value()));
}

Result<PrRefs>
BitbucketCloudClient::refs(const std::string& id) {
    Result<json> r = req_json(http_, cred_, "GET", pr_url(id));
    if (!r) {
        return Result<PrRefs>::err(r.error());
    }
    return Result<PrRefs>::ok(to_refs(r.value(), id));
}

Result<std::vector<PrFile>>
BitbucketCloudClient::files(const std::string& id) {
    Result<std::vector<json>> r = collect_all(http_, cred_, pr_url(id) + "/diffstat");
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
BitbucketCloudClient::commits(const std::string& id) {
    Result<std::vector<json>> r = collect_all(http_, cred_, pr_url(id) + "/commits");
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

// Group a flat comment list (PR or commit) into anchored threads. Bitbucket's PR
// and commit comment payloads share the same shape (content / inline / parent /
// user), so both threads() and commit_threads() funnel through here.
static std::vector<ReviewThread>
parse_comment_threads(const std::vector<json>& all) {
    // Index comments and their parent links, skipping deleted ones.
    std::unordered_map<std::string, const json*> by_id;
    std::unordered_map<std::string, std::string> parent_of;
    std::vector<std::string> order;
    for (const auto& c : all) {
        if (jbool(c, "deleted")) {
            continue;
        }
        const std::string cid = jid(c);
        if (cid.empty()) {
            continue;
        }
        by_id[cid] = &c;
        parent_of[cid] = jid(jchild(c, "parent"));
        order.push_back(cid);
    }

    // Walk parent links up to the thread root (Bitbucket points at the immediate
    // parent; a thread groups a root comment with all its descendants).
    auto root_of = [&](std::string cid) {
        std::unordered_set<std::string> seen;
        while (true) {
            auto it = parent_of.find(cid);
            if (it == parent_of.end() || it->second.empty() || !by_id.count(it->second) ||
                seen.count(cid)) {
                break;
            }
            seen.insert(cid);
            cid = it->second;
        }
        return cid;
    };

    std::unordered_map<std::string, ReviewThread> threads;
    std::vector<std::string> root_order;
    for (const std::string& cid : order) {
        const std::string root = root_of(cid);
        ReviewThread& th = threads[root];
        if (th.id.empty()) {
            th.id = root;
            root_order.push_back(root);
            const json& rc = *by_id[root];
            const json& inl = jchild(rc, "inline");
            if (inl.is_object() && !jstr(inl, "path").empty()) {
                RangeAnchor a;
                a.new_path = jstr(inl, "path");
                if (jchild(inl, "to").is_number_integer()) {
                    a.side = Side::New;
                    a.start.line = jint(inl, "to");
                } else if (jchild(inl, "from").is_number_integer()) {
                    a.side = Side::Old;
                    a.start.line = jint(inl, "from");
                }
                a.end = a.start;
                th.anchor = a;
            }
            th.resolved = jchild(rc, "resolution").is_object();
        }
        const json& c = *by_id[cid];
        Comment cm;
        cm.id = cid;
        cm.parent_id = parent_of[cid];
        cm.author = jstr(jchild(c, "user"), "display_name");
        cm.body_md = comment_body(jchild(c, "content"));
        cm.created = jstr(c, "created_on");
        th.comments.push_back(std::move(cm));
    }

    std::vector<ReviewThread> out;
    out.reserve(root_order.size());
    for (const std::string& root : root_order) {
        out.push_back(std::move(threads[root]));
    }
    return out;
}

Result<std::vector<ReviewThread>>
BitbucketCloudClient::threads(const std::string& id) {
    Result<std::vector<json>> r = collect_all(http_, cred_, pr_url(id) + "/comments");
    if (!r) {
        return Result<std::vector<ReviewThread>>::err(r.error());
    }
    return Result<std::vector<ReviewThread>>::ok(parse_comment_threads(r.value()));
}

Result<std::vector<ReviewThread>>
BitbucketCloudClient::commit_threads(const std::string& sha) {
    Result<std::vector<json>> r = collect_all(http_, cred_, commit_url(sha) + "/comments");
    if (!r) {
        return Result<std::vector<ReviewThread>>::err(r.error());
    }
    return Result<std::vector<ReviewThread>>::ok(parse_comment_threads(r.value()));
}

Result<std::string>
BitbucketCloudClient::file_at(const std::string& sha, const std::string& path) {
    HttpRequest rq;
    rq.method = "GET";
    rq.url = base_ + "/repositories/" + ws_ + "/" + repo_ + "/src/" + sha + "/" + path;
    add_auth(rq, cred_);
    const HttpResult res = http_.send(rq);
    log_line("[bitbucket] GET " + rq.url + " -> " +
             (res.ok() ? ("HTTP " + std::to_string(res.response.status))
                       : ("transport error: " + res.message)));
    if (auto e = http_error(res)) {
        return Result<std::string>::err(*e);
    }
    return Result<std::string>::ok(res.response.body);
}

// POST a comment (PR or commit) to `comments_url`. The body shape is identical for
// both endpoints: content.raw, an optional parent (reply), and an optional inline
// anchor (path + to/from). Inline anchors carry whatever line numbers the caller
// supplied — for a commit comment those are the commit's own diff lines.
static Result<Comment>
post_comment(HttpClient& http, const Credential& cred, const std::string& comments_url,
             const NewComment& nc) {
    json body;
    body["content"]["raw"] = nc.body_md;
    if (nc.reply_to) {
        try {
            body["parent"]["id"] = static_cast<std::int64_t>(std::stoll(*nc.reply_to));
        } catch (...) {
            return Result<Comment>::err({ErrorKind::Other, 0, "invalid reply_to comment id"});
        }
    } else if (!nc.general && !nc.anchor.new_path.empty()) {
        body["inline"]["path"] = nc.anchor.new_path;
        if (nc.anchor.side == Side::New) {
            body["inline"]["to"] = nc.anchor.start.line;
        } else {
            body["inline"]["from"] = nc.anchor.start.line;
        }
    }
    Result<json> r = req_json(http, cred, "POST", comments_url, body.dump());
    if (!r) {
        return Result<Comment>::err(r.error());
    }
    const json& j = r.value();
    Comment cm;
    cm.id = jid(j);
    cm.parent_id = jid(jchild(j, "parent"));
    cm.author = jstr(jchild(j, "user"), "display_name");
    cm.body_md = comment_body(jchild(j, "content"));
    cm.created = jstr(j, "created_on");
    return Result<Comment>::ok(cm);
}

Result<Comment>
BitbucketCloudClient::comment(const std::string& id, const NewComment& nc) {
    return post_comment(http_, cred_, pr_url(id) + "/comments", nc);
}

Result<Comment>
BitbucketCloudClient::comment_on_commit(const std::string& sha, const NewComment& nc) {
    return post_comment(http_, cred_, commit_url(sha) + "/comments", nc);
}

Result<void>
BitbucketCloudClient::approve(const std::string& id) {
    const HttpResult res = send(http_, cred_, "POST", pr_url(id) + "/approve", "");
    if (auto e = http_error(res)) {
        return Result<void>::err(*e);
    }
    return Result<void>::ok();
}

Result<void>
BitbucketCloudClient::unapprove(const std::string& id) {
    const HttpResult res = send(http_, cred_, "DELETE", pr_url(id) + "/approve", "");
    if (auto e = http_error(res)) {
        return Result<void>::err(*e);
    }
    return Result<void>::ok();
}

Result<void>
BitbucketCloudClient::request_changes(const std::string&) {
    // Not exposed (capabilities().request_changes_state == false).
    return Result<void>::err({ErrorKind::Unsupported, 0, "Bitbucket Cloud has no request-changes state"});
}

Result<void>
BitbucketCloudClient::submit_review(const std::string&, const Review&) {
    // No batched-review API (capabilities().pending_review_batch == false).
    return Result<void>::err({ErrorKind::Unsupported, 0, "Bitbucket Cloud has no batched review submit"});
}

ProviderPlugin
bitbucket_cloud_plugin() {
    ProviderPlugin p;
    p.id = "bitbucket-cloud";
    p.matches = [](const RemoteUrl& u) { return u.host.find("bitbucket.org") != std::string::npos; };
    p.make = [](const RemoteUrl& u, const RemoteConfig& cfg, HttpClient& http,
                const Credential& cred) -> std::unique_ptr<ReviewProvider> {
        const std::string base = cfg.base_url.empty() ? "https://api.bitbucket.org/2.0" : cfg.base_url;
        return std::make_unique<BitbucketCloudClient>(http, cred, u.owner, u.repo, base);
    };
    p.auth.methods = {AuthMethod::BasicToken};
    p.auth.scopes = {"read:pullrequest:bitbucket", "write:pullrequest:bitbucket"};
    p.auth.help_text =
        "Create a scoped API token (id.atlassian.com → Security → API tokens) with the "
        "Bitbucket Pull requests read/write scopes, and sign in with your Atlassian account "
        "email. App passwords are deprecated; Basic auth still carries the token.";
    return p;
}

}  // namespace diffy::review
