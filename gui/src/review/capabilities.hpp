#pragma once

// diffy_review — per-backend feature flags.
//
// The principle is "gate, don't branch": the PR toolbar, comment composer, and
// labels render FROM these flags, never from `if (bitbucket)`. Backends differ in
// what they can express (an explicit ChangesRequested state, suggestion blocks,
// thread resolution, batched reviews, comment granularity, merge strategies, even
// the noun — GitLab says "merge request"), and every one of those differences is
// captured here so the single UI adapts by reading data instead of forking on
// vendor. A provider returns its Capabilities once; the UI caches it and shapes
// affordances accordingly (e.g. "Request changes" hides where
// request_changes_state is false; a multi-line drag offers a range comment only
// where granularity != Line).

#include "model.hpp"

#include <string>
#include <vector>

namespace diffy::review {

// The finest anchor a backend's inline-comment API preserves. The UI's
// selection->comment affordance respects this: a multi-line drag offers a range
// comment where LineRange/CharRange, and otherwise snaps to a single line.
//   Line      -> Bitbucket Cloud   (keeps one line; drops end + col)
//   LineRange -> GitHub, GitLab    (keeps a line range; drops col)
//   CharRange -> Bitbucket Server, Azure DevOps (keeps char-precise offsets)
enum class CommentGranularity {
    Line,
    LineRange,
    CharRange,
};

// The feature descriptor a provider advertises. Defaults describe the most
// conservative backend (single-line comments, no batching, no explicit
// request-changes) so a minimal provider is safe out of the box.
struct Capabilities {
    // The user-facing noun for a review item. GitLab: "merge request".
    std::string item_noun = "pull request";

    // Finest inline-comment anchor the backend keeps (see enum above).
    CommentGranularity granularity = CommentGranularity::Line;

    // GitHub-style batched review: comments accumulate as "pending" and post on
    // submit_review(). When false, comment() posts immediately.
    bool pending_review_batch = false;

    // Backend has an explicit "changes requested" review state (GitHub, GitLab,
    // Azure DevOps). Bitbucket Cloud has no such state -> the toolbar hides it.
    bool request_changes_state = false;

    // Suggested-change blocks in comments (GitHub, GitLab).
    bool suggestions = false;

    // Resolve / unresolve threads.
    bool thread_resolution = false;

    // Backend has a commit-scoped comment API (distinct from PR comments; anchored
    // to a specific commit's diff). Drives whether selecting a PR commit offers a
    // commit-level composer instead of a PR-level one.
    bool commit_comments = false;

    // Draft / work-in-progress PRs are a first-class state.
    bool draft_items = false;

    // Merge strategies the backend offers (drives the merge confirm dialog).
    std::vector<MergeStrategy> merge_strategies;
};

}  // namespace diffy::review
