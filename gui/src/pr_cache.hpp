#pragma once

/*
    Persist the per-repository open-PR lists across application runs, so the PR
    sidebar populates instantly on launch. The cache is only a placeholder: the
    app always re-fetches in the background on repo open (and re-fetches a PR's
    detail — refs/commits/threads — whenever it is opened), so stale entries
    (new or replaced commits, merged/declined PRs) are corrected within moments.

    Stored as pr_cache.conf next to diffy.conf (same format family). Keyed by
    "workspace/repo".
*/

#include "review/model.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace diffy::gui {

using PrListCache = std::unordered_map<std::string, std::vector<diffy::review::PullRequest>>;

// Load the cached PR lists (empty on a missing/corrupt file — never throws).
PrListCache
pr_cache_load();

// Write the cached PR lists to disk (creating the config dir if needed).
void
pr_cache_save(const PrListCache& cache);

}  // namespace diffy::gui
