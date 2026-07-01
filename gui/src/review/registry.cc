// diffy_review — remote-URL parsing + provider registry (implementation).
//
// See registry.hpp for the WHY. parse_remote_url() is a small hand-rolled parser
// (no regex) that normalizes the three git remote shapes into host/owner/repo;
// the registry resolution is a plain ordered scan with an id-override shortcut.

#include "registry.hpp"

#include <string>
#include <string_view>

namespace diffy::review {

namespace {

// Trim leading/trailing ASCII whitespace.
std::string_view
trim(std::string_view s) {
    const auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && is_ws(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_ws(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Strip a single trailing ".git" if present.
std::string_view
strip_dot_git(std::string_view s) {
    constexpr std::string_view suffix = ".git";
    if (s.size() >= suffix.size() &&
        s.substr(s.size() - suffix.size()) == suffix) {
        s.remove_suffix(suffix.size());
    }
    return s;
}

// Drop a leading "user@" from a host authority (userinfo).
std::string_view
strip_userinfo(std::string_view authority) {
    const auto at = authority.find('@');
    if (at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }
    return authority;
}

// Drop a trailing ":port" from a host authority. Only trims when the segment
// after the last ':' is all digits, so it won't mangle an scp-like path.
std::string_view
strip_port(std::string_view host) {
    const auto colon = host.rfind(':');
    if (colon == std::string_view::npos) {
        return host;
    }
    bool all_digits = colon + 1 < host.size();
    for (std::size_t i = colon + 1; i < host.size(); ++i) {
        if (host[i] < '0' || host[i] > '9') {
            all_digits = false;
            break;
        }
    }
    if (all_digits) {
        host.remove_suffix(host.size() - colon);
    }
    return host;
}

// From a "owner/repo(/…)" path, take the first two non-empty segments as
// owner + repo (repo already had ".git" stripped by the caller). Returns false
// when fewer than two segments are present.
bool
split_owner_repo(std::string_view path, std::string& owner, std::string& repo) {
    // Skip a leading slash.
    while (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    const auto slash = path.find('/');
    if (slash == std::string_view::npos) {
        return false;
    }
    std::string_view owner_sv = path.substr(0, slash);
    std::string_view rest = path.substr(slash + 1);
    // repo is the next segment (ignore any further /… path).
    const auto next = rest.find('/');
    std::string_view repo_sv = (next == std::string_view::npos) ? rest : rest.substr(0, next);
    repo_sv = strip_dot_git(repo_sv);
    if (owner_sv.empty() || repo_sv.empty()) {
        return false;
    }
    owner.assign(owner_sv.begin(), owner_sv.end());
    repo.assign(repo_sv.begin(), repo_sv.end());
    return true;
}

// Parse a "scheme://[user@]host[:port]/owner/repo" URL. `after_scheme` is the
// text following "://".
std::optional<RemoteUrl>
parse_authority_form(std::string_view after_scheme) {
    const auto slash = after_scheme.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view authority = after_scheme.substr(0, slash);
    std::string_view path = after_scheme.substr(slash);  // keeps the leading '/'

    authority = strip_userinfo(authority);
    authority = strip_port(authority);
    if (authority.empty()) {
        return std::nullopt;
    }

    RemoteUrl url;
    url.host.assign(authority.begin(), authority.end());
    if (!split_owner_repo(path, url.owner, url.repo)) {
        return std::nullopt;
    }
    return url;
}

}  // namespace

std::optional<RemoteUrl>
parse_remote_url(const std::string& origin) {
    std::string_view s = trim(origin);
    if (s.empty()) {
        return std::nullopt;
    }

    // scheme://… forms: https://…, ssh://…, http://… — treat any "://" prefix as
    // an authority form.
    const auto scheme = s.find("://");
    if (scheme != std::string_view::npos) {
        return parse_authority_form(s.substr(scheme + 3));
    }

    // scp-like: [user@]host:owner/repo  — a ':' separates host from path, and the
    // path is not a port (i.e. there is a '/' in the part after the colon).
    const auto colon = s.find(':');
    if (colon != std::string_view::npos) {
        std::string_view authority = s.substr(0, colon);
        std::string_view path = s.substr(colon + 1);
        authority = strip_userinfo(authority);
        if (authority.empty()) {
            return std::nullopt;
        }
        RemoteUrl url;
        url.host.assign(authority.begin(), authority.end());
        if (!split_owner_repo(path, url.owner, url.repo)) {
            return std::nullopt;
        }
        return url;
    }

    return std::nullopt;
}

void
ProviderRegistry::register_plugin(ProviderPlugin plugin) {
    plugins_.push_back(std::move(plugin));
}

const ProviderPlugin*
ProviderRegistry::resolve(const RemoteUrl& url,
                          const std::optional<RemoteConfig>& override_cfg) const {
    // An override with a non-empty provider_id forces that plugin, ignoring host.
    if (override_cfg && !override_cfg->provider_id.empty()) {
        for (const auto& p : plugins_) {
            if (p.id == override_cfg->provider_id) {
                return &p;
            }
        }
        return nullptr;
    }

    // Otherwise: first plugin whose host heuristic matches, in registration order.
    for (const auto& p : plugins_) {
        if (p.matches && p.matches(url)) {
            return &p;
        }
    }
    return nullptr;
}

}  // namespace diffy::review
