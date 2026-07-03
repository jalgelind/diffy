#pragma once

// GitHub provider (REST v3, https://api.github.com).
//
// The SECOND concrete ReviewProvider and, per REVIEW-ROADMAP §10 (P0.5), the
// "abstraction proof": implemented against the SAME neutral model + conformance
// battery as Bitbucket Cloud, before any GitHub-specific UI. Two independent
// backends passing identical assertions is the only real test that the seam holds.
//
// GitHub specifics that shape the mapping (and exercise corners Bitbucket didn't):
//   * ids: a PR is addressed by its `number`; that is the neutral PullRequest::id.
//   * pagination: RFC-5988 `Link:` header (rel="next"), not a body `next` URL.
//   * auth: a PAT/OAuth token as `Authorization: Bearer …`; a User-Agent header is
//     mandatory or the API returns 403.
//   * inline comments keep a line RANGE + side (Capabilities::granularity ==
//     LineRange); general PR comments live on the issues endpoint.
//   * an explicit "changes requested" review state and a GitHub-style batched
//     review exist (request_changes_state / pending_review_batch both true), so
//     the gated-write conformance checks are skipped for GitHub.
//   * unapprove has no direct endpoint: it dismisses the caller's own APPROVED
//     review (found via /reviews + whoami), which the ReviewProvider::unapprove(id)
//     signature still expresses without change — a deliberate seam stress-test.

#include "../auth.hpp"
#include "../http_client.hpp"
#include "../registry.hpp"
#include "../review_provider.hpp"

#include <string>

namespace diffy::review {

class GitHubClient : public ReviewProvider {
  public:
    // `owner`/`repo` are the {owner}/{repo} from the parsed remote. `base_url`
    // defaults to the public API root; a GitHub Enterprise root (…/api/v3) can be
    // supplied via RemoteConfig at the registry layer.
    GitHubClient(HttpClient& http, Credential cred, std::string owner, std::string repo,
                 std::string base_url = "https://api.github.com");

    Capabilities capabilities() const override;
    Result<Account> whoami() override;
    Result<Page<PullRequest>> list_open(const std::string& cursor = "") override;
    Result<PullRequest> get(const std::string& id) override;
    Result<PrRefs> refs(const std::string& id) override;
    Result<std::vector<PrFile>> files(const std::string& id) override;
    Result<std::vector<PrCommit>> commits(const std::string& id) override;
    Result<std::vector<ReviewThread>> threads(const std::string& id) override;
    Result<std::vector<ReviewThread>> commit_threads(const std::string& sha) override;
    Result<std::string> file_at(const std::string& sha, const std::string& path) override;
    Result<Comment> comment(const std::string& id, const NewComment&) override;
    Result<Comment> comment_on_commit(const std::string& sha, const NewComment&) override;
    Result<Comment> edit_comment(const std::string& id, const std::string& comment_id,
                                 const std::string& body_md) override;
    Result<void> delete_comment(const std::string& id, const std::string& comment_id) override;
    Result<void> approve(const std::string& id) override;
    Result<void> unapprove(const std::string& id) override;
    Result<void> request_changes(const std::string& id) override;
    Result<void> submit_review(const std::string& id, const Review&) override;
    Result<void> merge(const std::string& id, MergeStrategy strategy,
                       const std::string& message = "") override;

  private:
    HttpClient& http_;
    Credential cred_;
    std::string owner_;
    std::string repo_;
    std::string base_;

    std::string repo_url() const;               // {base}/repos/{owner}/{repo}
    std::string pr_url(const std::string& id) const;  // …/pulls/{number}
};

// The registry plugin for GitHub (host match on github.com + the factory + PAT
// auth). Registered by the app during repo detection.
ProviderPlugin github_plugin();

}  // namespace diffy::review
