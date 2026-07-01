#pragma once

// diffy_review — remote-URL parsing + the provider registry.
//
// WHY: when a repo is opened we must decide which backend (if any) owns its
// `origin` remote, without hard-coding a switch on hostnames. Two problems: (1)
// git remotes come in several shapes (https, scp-like git@host:owner/repo,
// ssh://…) that all encode the same host/owner/repo, so parse_remote_url()
// normalizes them into a RemoteUrl; (2) a plain host heuristic can't recognize a
// corporate GHE/GitLab/BBS on a custom domain, so a per-repo RemoteConfig can
// *force* a provider + base URL. The ProviderRegistry is an ordered, first-match
// list of ProviderPlugins (id + host matcher + factory + AuthDescriptor);
// resolution honors an override by id first, else the first plugin that matches.
// The registry is the *mechanism* only — no concrete providers are registered
// here (they arrive in #27/#28). See REVIEW-ROADMAP.md §7 (Detection/self-hosting).

#include "auth.hpp"
#include "http_client.hpp"
#include "review_provider.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace diffy::review {

// The normalized triple every supported remote reduces to. `owner` is the
// workspace/org/user segment; on Bitbucket Cloud the path is /{workspace}/{repo},
// so host + owner + repo uniquely identify the repository across all providers.
struct RemoteUrl {
    std::string host;
    std::string owner;  // workspace / org / user
    std::string repo;
};

// Parse a git `origin` URL into a RemoteUrl, or std::nullopt if it can't be
// understood. A trailing ".git" is stripped. Handled forms:
//   * https://host/owner/repo(.git)        and https://user@host/owner/repo
//   * git@host:owner/repo(.git)            (scp-like)
//   * ssh://git@host[:port]/owner/repo(.git)
std::optional<RemoteUrl> parse_remote_url(const std::string& origin);

// A per-repo override for self-hosted deployments that host detection can't spot.
// When `provider_id` is non-empty it forces that plugin regardless of host, and
// `base_url` gives the API root; `account` selects which stored credential to use.
struct RemoteConfig {
    std::string provider_id;
    std::string base_url;
    std::string account;
};

// One registered backend: a stable `id` ("bitbucket-cloud", "github", …), a host
// heuristic, a factory that builds a live ReviewProvider from the resolved config
// + HTTP client + credential, and the auth methods it advertises to the UI.
struct ProviderPlugin {
    std::string id;
    std::function<bool(const RemoteUrl&)> matches;
    // Builds a live provider. Receives the parsed RemoteUrl (host/owner/repo — the
    // provider needs owner+repo to address the repository), the per-repo override
    // config, the shared HTTP client, and the resolved credential.
    std::function<std::unique_ptr<ReviewProvider>(const RemoteUrl&, const RemoteConfig&,
                                                  HttpClient&, const Credential&)>
        make;
    AuthDescriptor auth;
};

// An ordered, first-match-wins list of provider plugins. Register plugins in
// priority order; resolve() returns a borrowed pointer into the registry (valid
// for the registry's lifetime) or nullptr when nothing owns the remote.
class ProviderRegistry {
  public:
    // Append a plugin. Registration order is resolution order.
    void register_plugin(ProviderPlugin plugin);

    // Resolution rule: if `override_cfg` has a non-empty provider_id, return the
    // plugin whose id equals it (ignoring host); otherwise the first plugin whose
    // matches(url) is true; otherwise nullptr.
    const ProviderPlugin* resolve(const RemoteUrl& url,
                                  const std::optional<RemoteConfig>& override_cfg =
                                      std::nullopt) const;

  private:
    std::vector<ProviderPlugin> plugins_;
};

}  // namespace diffy::review
