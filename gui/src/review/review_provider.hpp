#pragma once

// diffy_review — the abstract provider interface.
//
// This is the one seam the whole layer turns on: each backend (Bitbucket Cloud
// first, GitHub second as the abstraction proof, then the rest) implements this
// interface, and the UI talks only to it plus the neutral model + Capabilities.
// Everything returns Result<T> so callers branch on normalized errors without
// exceptions; listing returns the neutral Page<T> (HTTP-level pagination is a
// provider-internal detail — the caller just passes next_cursor back in). The
// diff content itself is NOT sourced here: providers supply ref specs + SHAs via
// refs()/file_at() and the git layer re-diffs locally (see REVIEW-ROADMAP.md §4).
//
// The write methods are capability-gated: when the corresponding Capabilities
// flag is off, an implementation returns Error{ErrorKind::Unsupported, ...}
// rather than crashing, so the UI can call uniformly and rely on the normalized
// error to no-op gracefully.

#include "capabilities.hpp"
#include "model.hpp"
#include "result.hpp"

#include <string>
#include <vector>

namespace diffy::review {

struct ReviewProvider {
    virtual ~ReviewProvider() = default;

    // The backend's feature flags. Cheap/const; the UI caches the result.
    virtual Capabilities capabilities() const = 0;

    // The authenticated account (drives the "needs your review" grouping).
    virtual Result<Account> whoami() = 0;

    // Whether the authenticated user may MERGE pull requests in this repo. Merging
    // needs write access, which is strictly more than the read scope browsing needs;
    // this lets the UI hide a merge affordance the token/role can't exercise instead
    // of surfacing it and letting the API 403 on click. Repo-scoped and cacheable.
    //
    // The default (and the value on any inability to determine it — missing scope,
    // network error) is PERMISSIVE (true): this may only ever HIDE a merge the user
    // genuinely can't perform, never gate browsing or a merge the user actually can
    // do. Providers override it with a cheap repo-permission probe.
    virtual Result<bool> viewer_can_merge() {
        return Result<bool>::ok(true);
    }

    // --- read: listing & PR data ------------------------------------------

    // One page of open PRs. Pass the previous page's next_cursor to continue;
    // "" starts from the first page.
    virtual Result<Page<PullRequest>> list_open(const std::string& cursor = "") = 0;

    virtual Result<PullRequest> get(const std::string& id) = 0;

    // Ref specs + SHAs for local fetch/diff.
    virtual Result<PrRefs> refs(const std::string& id) = 0;

    // Neutral diffstat for the PR.
    virtual Result<std::vector<PrFile>> files(const std::string& id) = 0;

    // The PR's commits (for per-commit narrowing, PrCommit view).
    virtual Result<std::vector<PrCommit>> commits(const std::string& id) = 0;

    // Inline + general comment threads, already anchored.
    virtual Result<std::vector<ReviewThread>> threads(const std::string& id) = 0;

    // Comment threads scoped to a specific commit (distinct from PR threads;
    // anchored to that commit's own diff, so their line numbers are only valid in
    // the commit view, never the PR aggregate). Backends without a commit-comment
    // API (Capabilities::commit_comments == false) return Unsupported.
    virtual Result<std::vector<ReviewThread>> commit_threads(const std::string& sha) = 0;

    // Fallback content source when the commits can't be fetched locally.
    virtual Result<std::string> file_at(const std::string& sha, const std::string& path) = 0;

    // --- write: capability-gated (return Unsupported when the flag is off) --

    // Post a comment. A reply is just a NewComment with `reply_to` set to the
    // parent comment id; an inline comment carries an anchor; a PR-level comment
    // sets `general`.
    virtual Result<Comment> comment(const std::string& id, const NewComment&) = 0;

    // Post a comment scoped to a commit. Inline anchors use that commit's own line
    // numbers (valid only against the commit's diff), so this never mis-anchors on
    // the PR head. Unsupported when Capabilities::commit_comments is false.
    virtual Result<Comment> comment_on_commit(const std::string& sha, const NewComment&) = 0;

    // Edit an existing comment's body. `id` is the PR id, `comment_id` the target
    // comment; returns the updated comment. (Authorization — you can only edit your
    // own — is enforced by the backend; a forbidden edit returns Error, not a crash.)
    virtual Result<Comment> edit_comment(const std::string& id, const std::string& comment_id,
                                         const std::string& body_md) = 0;

    // Delete an existing comment. `id` is the PR id, `comment_id` the target.
    virtual Result<void> delete_comment(const std::string& id, const std::string& comment_id) = 0;

    virtual Result<void> approve(const std::string& id) = 0;
    virtual Result<void> unapprove(const std::string& id) = 0;
    virtual Result<void> request_changes(const std::string& id) = 0;
    virtual Result<void> submit_review(const std::string& id, const Review&) = 0;

    // Merge the PR with one of Capabilities::merge_strategies. Outward-facing and
    // irreversible, so the UI always confirms first (§7 write-safety). `message` is
    // an optional merge-commit message (ignored by fast-forward). Unsupported when
    // merge_strategies is empty.
    virtual Result<void> merge(const std::string& id, MergeStrategy strategy,
                               const std::string& message = "") = 0;
};

}  // namespace diffy::review
