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

    // Fallback content source when the commits can't be fetched locally.
    virtual Result<std::string> file_at(const std::string& sha, const std::string& path) = 0;

    // --- write: capability-gated (return Unsupported when the flag is off) --

    virtual Result<Comment> comment(const std::string& id, const NewComment&) = 0;
    virtual Result<void> approve(const std::string& id) = 0;
    virtual Result<void> unapprove(const std::string& id) = 0;
    virtual Result<void> request_changes(const std::string& id) = 0;
    virtual Result<void> submit_review(const std::string& id, const Review&) = 0;
};

}  // namespace diffy::review
