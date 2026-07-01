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
    // Non-empty when the content shouldn't be diffed as text (binary, or larger
    // than the diff size cap); the frontend shows this instead of a diff.
    std::string note;
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
    // URL of the "origin" remote (empty if there is none). Used to detect which
    // hosted-review backend, if any, owns this repository.
    std::string
    origin_url() const;

    // Working-tree + index changes (porcelain-style status).
    std::vector<FileChange>
    status() const;

    // Commits reachable from HEAD (newest first), skipping the first `skip` and
    // returning up to `max_count` (or all of the rest when max_count <= 0).
    std::vector<CommitInfo>
    commits(int skip, int max_count) const;

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

    // --- write operations -------------------------------------------------
    // These mutate the repository (index / working tree / refs). All return
    // false on libgit2 error so the UI can report failure and re-scan. They are
    // deliberately UI-agnostic so a non-Slint frontend can drive them too.

    // Stage `path`: add it to the index, or stage its deletion if it's gone.
    bool
    stage_file(const std::string& path) const;

    // Unstage `path`: reset its index entry to HEAD (or remove it if unborn).
    bool
    unstage_file(const std::string& path) const;

    // Discard working-tree changes to `path`: restore it from HEAD, or delete it
    // if it's untracked. Destructive — the UI should confirm first.
    bool
    discard_changes(const std::string& path) const;

    // Commit the currently-staged index with `message`. `amend` replaces HEAD.
    bool
    commit(const std::string& message, bool amend = false) const;

    struct BranchInfo {
        std::string name;
        bool current = false;
        bool remote = false;
    };

    // Local + remote branches; `current` marks the checked-out one.
    std::vector<BranchInfo>
    branches() const;

    // Check out a local branch by short name. Fails (false) if the working tree
    // is dirty in a conflicting way (safe checkout), guarding against data loss.
    bool
    checkout_branch(const std::string& name) const;

    // Detach HEAD at `commit_oid` (view a historical commit). Safe checkout —
    // refuses to clobber a dirty working tree.
    bool
    checkout_commit(const std::string& commit_oid) const;

    // Create local branch `name` at `commit_oid` and check it out (safe checkout).
    bool
    create_branch_at(const std::string& name, const std::string& commit_oid) const;

    // old = `base_ref`'s blob for `path`, new = current on-disk content. Lets the
    // UI diff the working tree against an arbitrary ref instead of just HEAD.
    BlobPair
    diff_ref_file(const std::string& base_ref, const std::string& path) const;

   private:
    explicit Repo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;
};

}  // namespace diffy::gui
