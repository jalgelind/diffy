#pragma once

/*
    Thin libgit2 wrapper. Its job is to enumerate a repository and hand back the
    *content* of files at two revisions; it does no diffing itself — that is
    libdiffy's job. Keeping this boundary is what lets the GUI reuse the exact
    same diff engine as the CLI.
*/

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct git_repository;

namespace diffy::gui {

// One changed path in the working tree / index.
struct FileChange {
    std::string path;
    std::string status;  // short code: "M", "A", "D", "R", "?", "MM", ...
    bool staged = false;
};

struct CommitInfo {
    std::string oid;       // full hex id
    std::string short_oid;
    std::string summary;
    std::string author;
    int64_t time = 0;      // unix seconds
};

// The two sides to feed into the diff engine, plus display names.
struct BlobPair {
    bool ok = false;
    std::string old_text;
    std::string new_text;
    std::string old_name;
    std::string new_name;
};

// Process-wide libgit2 lifecycle.
void
git_runtime_init();
void
git_runtime_shutdown();

class Repo {
   public:
    static std::optional<Repo>
    open(const std::string& path);

    ~Repo();
    Repo(Repo&& other) noexcept;
    Repo& operator=(Repo&& other) noexcept;
    Repo(const Repo&) = delete;
    Repo& operator=(const Repo&) = delete;

    std::string
    workdir() const;
    std::string
    head_branch() const;

    // Working-tree + index changes (porcelain-style status).
    std::vector<FileChange>
    status() const;

    // Most recent commits reachable from HEAD (newest first).
    std::vector<CommitInfo>
    recent_commits(int max_count) const;

    // The files changed by `commit_oid` relative to its first parent.
    std::vector<FileChange>
    commit_files(const std::string& commit_oid) const;

    // old = HEAD blob for `path` (empty if newly added);
    // new = the file's current on-disk content (empty if deleted).
    BlobPair
    diff_workdir_file(const std::string& path) const;

    // old = parent-commit blob, new = commit blob for `path`.
    BlobPair
    diff_commit_file(const std::string& commit_oid, const std::string& path) const;

   private:
    explicit Repo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;
};

}  // namespace diffy::gui
