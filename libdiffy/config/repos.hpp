#pragma once

/*
    Persistence for the list of git repositories the GUI has opened, stored in
    `repos.conf` next to `diffy.conf`. Recent-first ordering; pinned repos are
    flagged so the UI can keep them at the top.
*/

#include <string>
#include <vector>

namespace diffy {

struct RepoEntry {
    std::string path;        // absolute, canonicalised path to the repo working dir
    std::string name;        // display name (basename of path)
    bool pinned = false;
};

// Absolute path to repos.conf.
std::string
repos_config_path();

// Load repos.conf. Missing/invalid file yields an empty list.
std::vector<RepoEntry>
repos_load();

// Write the list to repos.conf (creating the config dir if needed).
void
repos_save(const std::vector<RepoEntry>& repos);

// Promote `path` to most-recent (front), de-duplicating and canonicalising.
void
repos_add(std::vector<RepoEntry>& repos, const std::string& path);

void
repos_remove(std::vector<RepoEntry>& repos, const std::string& path);

void
repos_set_pinned(std::vector<RepoEntry>& repos, const std::string& path, bool pinned);

}  // namespace diffy
