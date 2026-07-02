#pragma once

// diffy_review — the neutral domain model.
//
// These types are shaped by the review *workflow*, not by any one provider's
// JSON. The UI and navigation bind here (plus capabilities.hpp) and never to a
// backend's wire format, so adding GitHub/GitLab/Bitbucket Server/Azure DevOps
// is "implement the interface + map onto these structs" with no UI change. This
// header is deliberately decoupled: it does NOT depend on repo_model, libgit2,
// Slint, or even result.hpp — it is pure value types over standard strings.
// (We intentionally do NOT reuse repo_model's FileChange/CommitInfo here; a
// duplicated, provider-blind PrFile/PrCommit keeps this lib free of the GUI's
// git model.)
//
// The make-or-break abstraction is RangeAnchor (see REVIEW-ROADMAP.md §4): the
// UI turns a text selection into an absolute {side, start-line/col, end-line/col}
// anchor; each provider merges in the DiffContext it captured and clamps the
// range to its own granularity. Anchoring by absolute line + side (not a diff
// "position") is what lets a comment survive our local re-diff choosing
// different hunk boundaries than the server.

#include <optional>
#include <string>
#include <vector>

namespace diffy::review {

// Which side of the diff a position/anchor refers to. A comment always anchors
// to exactly ONE side.
enum class Side {
    Old,
    New,
};

// The review state of a pull request, normalized across backends. Not every
// backend uses every value (e.g. Bitbucket Cloud has no explicit
// ChangesRequested state — see Capabilities::request_changes_state).
enum class ApprovalState {
    Open,
    Approved,
    ChangesRequested,
    Draft,
    Merged,
    Declined,
};

// A 1-based position inside a file. `line` is the logical file line (1-based);
// `col`, when present, is a 1-based column within that LOGICAL line (not a
// word-wrapped visual row). Absent `col` means the whole line.
struct TextPos {
    int line = 0;
    std::optional<int> col;
};

// The SHAs a provider captured when the PR was loaded. Opaque to the UI; a
// provider merges this into every RangeAnchor it produces or consumes so the
// server can resolve the anchor against the correct revisions.
struct DiffContext {
    std::string base_sha;
    std::string head_sha;
    std::string start_sha;
};

// The anchoring abstraction. `new_path` is the file on the new side; `old_path`
// is set for renames. `side` picks the side the comment lives on. `start`/`end`
// bound the range: start == end && !start.col => a whole-line anchor. Providers
// clamp [start,end] to their granularity (char range / line range / single
// line). `ctx` carries the SHAs (opaque to the UI).
struct RangeAnchor {
    std::string new_path;
    std::optional<std::string> old_path;
    Side side = Side::New;
    TextPos start;
    TextPos end;
    DiffContext ctx;
};

// A user/account on the backend. `id` is the stable provider id; `username` the
// handle; `display_name` for presentation.
struct Account {
    std::string id;
    std::string username;
    std::string display_name;
};

// A pull request (GitLab: "merge request" — see Capabilities::item_noun). `state`
// + `draft` drive the approval glyph and toolbar; `updated` is ISO-8601.
// A reviewer on a PR and whether they've approved (drives the "needs your review"
// grouping when matched against the authenticated account).
struct Reviewer {
    std::string id;  // provider account id
    bool approved = false;
};

struct PullRequest {
    std::string id;
    std::string title;
    std::string description;
    std::string author;
    std::string author_id;  // provider account id (for the "yours" grouping)
    std::string src_branch;
    std::string dst_branch;
    ApprovalState state = ApprovalState::Open;
    bool draft = false;
    int comment_count = 0;
    std::string updated;  // ISO-8601
    std::vector<Reviewer> reviewers;
};

// The refs + SHAs the git layer needs to fetch and diff a PR locally. The
// aggregate diff is rendered as merge-base(base,head)..head (three-dot).
struct PrRefs {
    std::string head_ref;
    std::string base_ref;
    std::string head_sha;
    std::string base_sha;
};

// A neutral diffstat entry for one changed file. Intentionally NOT repo_model's
// FileChange (keeps this lib decoupled from the GUI's git model). `old_path` is
// set for renames; `status` is one of added/modified/deleted/renamed.
struct PrFile {
    std::string path;
    std::optional<std::string> old_path;
    std::string status;  // added / modified / deleted / renamed
    int additions = 0;
    int deletions = 0;
};

// A commit belonging to a PR. Intentionally NOT repo_model's CommitInfo.
// `when` is an ISO-8601 timestamp.
struct PrCommit {
    std::string sha;
    std::string short_sha;
    std::string summary;
    std::string author;
    std::string when;
};

// A single comment. A non-empty `parent_id` marks this as a reply within its
// thread. `body_md` is Markdown; `created` is ISO-8601.
struct Comment {
    std::string id;
    std::string parent_id;
    std::string author;
    std::string author_avatar;  // URL of the author's avatar (may be empty)
    std::string body_md;
    std::string created;
};

// A comment thread anchored to the diff. `outdated` means the anchor no longer
// maps cleanly onto the current head (render in the collapsed "Outdated"
// section rather than mis-anchoring); `resolved` reflects thread resolution
// where the backend supports it (Capabilities::thread_resolution).
struct ReviewThread {
    std::string id;
    RangeAnchor anchor;
    bool outdated = false;
    bool resolved = false;
    std::vector<Comment> comments;
};

// A comment the UI wants to post. For an inline comment, `anchor` locates it and
// `general` is false. For a whole-file / PR-level comment, set `general = true`
// (the `anchor` is then ignored). `reply_to`, when set, makes this a reply to an
// existing comment id.
struct NewComment {
    RangeAnchor anchor;
    std::string body_md;
    std::optional<std::string> reply_to;
    bool general = false;
};

// How a PR gets merged; advertised per backend via
// Capabilities::merge_strategies.
enum class MergeStrategy {
    MergeCommit,
    Squash,
    FastForward,
    Rebase,
};

// A batched review submission (used where Capabilities::pending_review_batch):
// an overall body + verdict plus the pending inline comments to post atomically.
struct Review {
    std::string body_md;
    ApprovalState verdict = ApprovalState::Open;
    std::vector<NewComment> comments;
};

}  // namespace diffy::review
