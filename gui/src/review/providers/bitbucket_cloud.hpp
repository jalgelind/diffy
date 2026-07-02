#pragma once

// Bitbucket Cloud provider (REST 2.0, https://api.bitbucket.org/2.0).
//
// The first concrete ReviewProvider — and, per REVIEW-ROADMAP §10 (P0.5), the
// task that also stands in for the old "live spike": its first real call is the
// end-to-end validation of the HTTP + auth + JSON + pagination path. It is built
// fixture-first (exercised offline against MockHttpClient + the conformance
// battery) so it can ship and be trusted before any live credentials exist.
//
// Bitbucket Cloud specifics that shape the mapping: PR ids are integers; listing
// paginates via a full `next` URL alongside `values`; inline comments anchor by
// `inline.to` (new side) / `inline.from` (old side); there is no explicit
// "changes requested" review state (Capabilities::request_changes_state = false,
// so request_changes() returns Unsupported). Diff *content* is not fetched here —
// refs() hands the git layer the SHAs; file_at() is only the raw-blob fallback.

#include "../auth.hpp"
#include "../http_client.hpp"
#include "../registry.hpp"
#include "../review_provider.hpp"

#include <string>

namespace diffy::review {

class BitbucketCloudClient : public ReviewProvider {
  public:
    // `workspace`/`repo_slug` are the {owner}/{repo} from the parsed remote.
    // `base_url` defaults to the public API root; a self-hosted/proxy root can be
    // supplied via RemoteConfig at the registry layer.
    BitbucketCloudClient(HttpClient& http, Credential cred, std::string workspace,
                         std::string repo_slug,
                         std::string base_url = "https://api.bitbucket.org/2.0");

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
    Result<void> approve(const std::string& id) override;
    Result<void> unapprove(const std::string& id) override;
    Result<void> request_changes(const std::string& id) override;
    Result<void> submit_review(const std::string& id, const Review&) override;

  private:
    HttpClient& http_;
    Credential cred_;
    std::string ws_;
    std::string repo_;
    std::string base_;

    std::string pr_url(const std::string& id) const;  // {base}/repositories/{ws}/{repo}/pullrequests/{id}
    std::string commit_url(const std::string& sha) const;  // {base}/repositories/{ws}/{repo}/commit/{sha}
};

// The registry plugin for Bitbucket Cloud (host match on bitbucket.org + the
// factory + advertised auth). Registered by the app during repo detection (P1).
ProviderPlugin bitbucket_cloud_plugin();

}  // namespace diffy::review
