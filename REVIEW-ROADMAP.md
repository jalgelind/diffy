# REVIEW-ROADMAP — Code review over hosted pull requests

> **Status: planned.** Nothing here is built yet. This document is the agreed
> design for adding pull-request review to `diffy-gui`. Bitbucket Cloud is the
> first implementation, but the layer is deliberately backend-agnostic: adding
> GitHub / GitLab / Bitbucket Server / Azure DevOps later must be "implement one
> interface + ship fixtures," with **no UI changes**.

`diffy-gui` already has the hard parts of review: a fast diff renderer,
side-by-side/unified modes, hunk navigation, syntax + intra-line token
highlighting, and — since the one-row-per-visual-line model — a row model whose
absolute `old_lineno`/`new_lineno` make per-line comment anchoring exact. What is
missing is *sourcing* pull requests and *round-tripping* review comments/approvals
on top of the diff we already draw. This plan adds exactly that.

---

## 1. Guiding principles

- **One review workflow, N backends.** The UI and navigation bind to a neutral
  domain model and a `Capabilities` descriptor — never to a provider's JSON or
  to `if (bitbucket)`. Backends differ (see §4); we *gate* on capabilities, we
  do not branch on vendor.
- **Diff content stays git-native.** We do not render a server's unified diff.
  We fetch the PR's commits (via git) and re-diff locally with the existing
  pipeline, preserving full-file syntax highlighting and token-level annotation.
  Providers supply only *ref specs + SHAs*; the diff engine is provider-blind.
- **The network layer is Slint-free and unit-tested.** It lives beside
  `repo_model` and joins `diffy-gui-logic-test`. A shared conformance suite runs
  every provider against recorded fixtures — no live network in CI.
- **Reuse the existing async + UI seams.** All calls run on the existing
  `load_threads` + `slint::invoke_from_event_loop` path. The PR panels reuse the
  `CommitsPanel`/`FilesPanel` components and the pinned-entry pattern.
- **Secrets never touch config.** Tokens live in the OS credential store; config
  holds only the account handle + base URL.

---

## 2. Layering

```
gui/src/review/
  model.hpp            neutral domain types (no provider JSON leaks here)
  capabilities.hpp     per-backend feature flags
  review_provider.hpp  the interface
  auth.hpp             AuthDescriptor, Credential, OAuth device-flow hook
  registry.{hpp,cc}    remote URL -> provider plugin (self-hosted overrides)
  http_client.{hpp,cc} libcurl wrapper; hides pagination styles + backoff
  secret_store.{hpp,cc} interface + per-OS impl (Win/Mac)
  conformance.hpp      shared test suite every provider must pass
  providers/
    bitbucket_cloud.cc   (ship first)
    github.cc            (build second — the abstraction's proof)
    gitlab.cc  bitbucket_server.cc  azure_devops.cc   (later)
```

Built as a static lib `diffy_review`, linked by `diffy-gui` and by
`diffy-gui-logic-test`. The UI targets `model.hpp` + `capabilities.hpp` only.

---

## 3. Neutral domain model

Shaped by the review *workflow*, not by any one API.

```cpp
enum class Side { Old, New };
enum class ApprovalState { Open, Approved, ChangesRequested, Draft, Merged, Declined };

struct PullRequest {
  std::string id, title, description, author;
  std::string src_branch, dst_branch;
  ApprovalState state; bool draft;
  int comment_count; std::string updated;      // ISO-8601
};

struct DiffContext { std::string base_sha, head_sha, start_sha; }; // provider-populated

struct LineAnchor {                 // the make-or-break abstraction (see §4)
  std::string new_path;
  std::optional<std::string> old_path;   // renames
  Side side; int line;                    // what the UI produces from a clicked row
  DiffContext ctx;                        // SHAs; opaque to the UI
};

struct Comment { std::string id, parent_id, author, body_md, created; };
struct ReviewThread { std::string id; LineAnchor anchor; bool outdated, resolved;
                      std::vector<Comment> comments; };

struct PrRefs { std::string head_ref, base_ref, head_sha, base_sha; };
```

Reused from `repo_model`: `FileChange` (diffstat) and `CommitInfo` (PR commits).

---

## 4. The anchoring abstraction (why the seam holds)

The UI only ever builds `{new_path, side, line}` from the row a user clicks
(`new_lineno` → `New`, `old_lineno` → `Old`). Each provider merges in the
`DiffContext` it captured when loading the PR. Because we anchor by **absolute
line + side** (not a diff "position"), it survives our local re-diff choosing
different hunk boundaries than the server, and it is the one shape all backends
can consume:

| Backend            | Inline-comment API shape                                             | Pulled from anchor        |
|--------------------|---------------------------------------------------------------------|---------------------------|
| Bitbucket Cloud    | `inline:{path, to \| from}`                                         | path + side/line          |
| Bitbucket Server   | `anchor:{path, line, lineType, fileType}`                           | path + side/line          |
| GitHub             | `{path, line, side, start_line?, commit_id}`                        | + `ctx.head_sha`          |
| GitLab             | `position:{base_sha,start_sha,head_sha, old_path,new_path, old_line/new_line}` | + all three SHAs |
| Azure DevOps       | `threadContext:{filePath, right/leftFileStart..End}`                | path + side/line → range  |

**Content sourcing** is likewise generic — only the ref naming differs:

| Backend          | head ref                          |
|------------------|-----------------------------------|
| GitHub           | `refs/pull/{id}/head`             |
| GitLab           | `refs/merge-requests/{id}/head`   |
| Bitbucket Cloud  | `refs/pull-requests/{id}/from`    |
| Bitbucket Server | `refs/pull-requests/{id}/from`    |

`refs()` returns `{head_ref, base_ref, head_sha, base_sha}`; the git layer fetches
them and renders the aggregate diff as **`merge-base(base,head)..head`**
(three-dot — only the PR's own changes, computed locally by libgit2). An API
`file_at(sha, path)` fallback covers the rare case where the commits can't be
fetched.

---

## 5. Capabilities (gate, don't branch)

```cpp
struct Capabilities {
  std::string item_noun = "pull request";  // GitLab: "merge request"
  bool multi_line_comments;    // GH start_line, GL, BBS ranges; BB Cloud: false
  bool pending_review_batch;   // GH batched review; else comments post immediately
  bool request_changes_state;  // GH/GL/ADO yes; BB Cloud: no explicit state
  bool suggestions;            // GH/GL suggested-change blocks
  bool thread_resolution;      // resolve/unresolve threads
  bool draft_items;
  std::vector<MergeStrategy> merge_strategies;
};
```

The PR-detail toolbar, comment composer, and labels render **from these flags**:
"Request changes" hides on Bitbucket Cloud, "Suggest change" appears only where
`suggestions`, the noun flips to "Merge request" on GitLab. One UI, N backends.

---

## 6. The interface

```cpp
struct ReviewProvider {
  virtual Capabilities capabilities() const = 0;
  virtual Result<Account> whoami() = 0;                         // for "needs your review"
  virtual Result<std::vector<PullRequest>> list_open(Pager&) = 0;
  virtual Result<PullRequest> get(const std::string& id) = 0;
  virtual Result<PrRefs> refs(const std::string& id) = 0;
  virtual Result<std::vector<FileChange>> files(const std::string& id) = 0;   // diffstat
  virtual Result<std::vector<CommitInfo>> commits(const std::string& id) = 0;
  virtual Result<std::vector<ReviewThread>> threads(const std::string& id) = 0;
  virtual Result<std::string> file_at(const std::string& sha,
                                      const std::string& path) = 0;           // fallback source
  // capability-gated — return Unsupported (never crash) when the flag is false:
  virtual Result<Comment> comment(const std::string& id, const NewComment&) = 0;
  virtual Result<void>    approve(const std::string& id) = 0;
  virtual Result<void>    unapprove(const std::string& id) = 0;
  virtual Result<void>    request_changes(const std::string& id) = 0;
  virtual Result<void>    submit_review(const std::string& id, const Review&) = 0; // if batched
};
```

- `Pager` hides page-number vs cursor vs `Link`-header pagination inside each impl.
- `Result<T>` = value or a **normalized** error kind: `Auth`, `RateLimited{retry_after}`,
  `NotFound`, `Unsupported`, `Network` — so the UI reacts uniformly.

---

## 7. Detection, auth, freshness

**Registry / self-hosting.** An ordered list of plugins, first match wins:

```cpp
struct ProviderPlugin { std::string id;
  bool matches(const RemoteUrl&);          // github.com, gitlab.com, bitbucket.org, …
  std::unique_ptr<ReviewProvider> make(RemoteConfig, HttpClient&, SecretStore&);
  AuthDescriptor auth; };                  // basic / PAT / OAuth device flow
```

Detection runs in `apply_files` (where every repo-open converges). Corporate
GHE/GitLab/BBS on custom domains can't be host-detected → per-repo config
override `{provider: gitlab, base_url: https://git.corp}`. Unknown remote → no PR
panel (today's behavior).

**Auth.** `AuthDescriptor` advertises accepted methods; `Credential{scheme,
principal, secret}` is generic; `SecretStore` keyed by `{provider_id, base_url,
account}`. Providers that support **OAuth device flow** implement
`begin_device_auth()` returning a verification URL + code the "Connect" card
shows. Bitbucket ships first with an App Password / API token through the same
card. Scopes: read + write pull requests.

**Freshness & caching.** Cache the PR list with a short TTL; cache a PR's diff and
threads keyed by `head_sha`. A **force-push changes `head_sha`** → the cache key
misses → we re-fetch refs, re-diff, and re-anchor (stale threads Bitbucket/GitHub
flag `outdated` land in a collapsed "Outdated (n)" section rather than
mis-anchoring). Manual refresh available; the existing soft-refresh timer does
not touch PR state.

**Offline / degrade.** Local-first sourcing means already-fetched PR refs still
diff and render with no network. List/threads/actions surface a neutral `Network`
banner and keep the last-good cache visible.

**Write-safety.** Approvals, request-changes, and merge are outward-facing.
Comments post on explicit submit; merge always confirms first; all write methods
are gated behind their capability flag *and* their phase (§10).

---

## 8. UI & navigation

One `nav-mode` state drives both sidebars (generalizes the `on-working-tree`
flag already added):

| State              | Left sidebar          | Right sidebar                              | Commit bar |
|--------------------|-----------------------|--------------------------------------------|------------|
| `LocalWorkingTree` | working-tree CHANGES  | Uncommitted-changes entry + Recent commits | shown      |
| `LocalCommit`      | commit's files        | Recent commits (selected)                  | hidden     |
| `PrList`           | (working tree)        | **open PRs, grouped**                      | hidden     |
| `PrDetail`         | **PR changed files**  | **‹ back · PR header · PR commits**        | hidden     |
| `PrCommit`         | commit's files        | PR commits (selected)                      | hidden     |

The commit bar's existing gate (`on-working-tree && staged>0`) hides it in every
PR state for free.

**Add-repo integration.** No separate "add remote" flow. You add the local clone
as usual; `apply_files` auto-detects the provider from `origin`:
- not a known host → right sidebar stays as today;
- known host + creds → background `list_open` → right sidebar shows `PrList`;
- known host, no creds → a **"Connect"** card where PRs will appear (token/OAuth →
  `SecretStore`, then retry). First-run onboarding lives exactly where it pays off.

**`PrList` structure.** Reuses the `CommitsPanel` lazy-load ListView + section
headers (like the STAGED/CHANGES split):
- groups **Needs your review** (you're a reviewer, not yet approved) / **Yours** /
  **Others**, sorted updated-desc;
- row: title, `#id`, author initials, `src → dst`, approval glyph, comment count,
  relative time;
- a bottom **`History ›`** entry switches to the classic Recent-commits view, so
  local history stays reachable on a hosted repo.

**`PrDetail`.** A shallow stack with a breadcrumb header (`‹ Pull requests`),
the PR header (title, `src → dst`, approval), the PR's **commits**, and (phase 4)
Approve / Request-changes actions rendered per `Capabilities`. Selecting a file
on the left opens the aggregate diff; selecting a commit narrows to `PrCommit`.

**Backend/Slint additions (additive):**
`nav-mode`, `pull-requests:[PrEntry]`, `pr-commits:[CommitEntry]`,
`current-pr:PrHeader`, `bitbucket-connected:bool`; callbacks `list-prs`,
`select-pr(id)`, `back-to-pr-list`, `select-pr-commit(oid)`,
`connect-provider(token)`, `approve-pr(id)`, `request-changes(id)`,
`add-review-comment(anchor,text)`. `CommitsPanel` becomes a light router on
`nav-mode` over three sub-views (`PrListView`, `PrDetailView`, existing
`HistoryView`).

---

## 9. Dependencies (Mac + Windows)

| Concern      | Windows                       | macOS                                   | CMake                                    |
|--------------|-------------------------------|-----------------------------------------|------------------------------------------|
| HTTP         | vcpkg libcurl (Schannel)      | vcpkg/brew libcurl (SecureTransport)    | `find_package(CURL)` → `CURL::libcurl`   |
| JSON         | submodule `subprojects/json`  | same                                    | `nlohmann_json::nlohmann_json`           |
| Secret store | `Advapi32` (CredRead/Write)   | `Security.framework` (Keychain; already linked) | per-OS `target_link_libraries`   |

TLS handled by libcurl's platform backend — no OpenSSL to ship. Credential code
sits behind `SecretStore` with `#if defined(_WIN32)/__APPLE__`, mirroring the
existing Win32-helpers-in-`main.cpp` / APPLE-frameworks-in-CMake split.

---

## 10. Phases (each independently shippable)

- **P0 — Plumbing.** `diffy_review` lib; deps wired both OSes; `HttpClient`+`Pager`;
  `SecretStore` (Win/Mac); `model`/`capabilities`/`review_provider`/`auth`/`registry`;
  conformance skeleton. *Exit:* a `list-open-prs` spike prints PRs given creds;
  fixture-based unit tests for parse, pagination, and anchor round-trip pass.
- **P0.5 — Abstraction proof.** Implement **Bitbucket Cloud** *and* **GitHub**
  against the same conformance fixtures *before* UI. Two impls is the only real
  test that the seam holds. *Exit:* both pass identical conformance assertions.
- **P1 — Read-only browse.** Detection in `apply_files`; Connect card; right-sidebar
  router; `PrList` grouping; `PrDetail` (PR header + commits + back); left-panel PR
  files; file open via local-first sourcing (fetch refs → `diff_oids`, three-dot).
  *Exit:* pick a hosted repo → open PRs → open one → browse every file with full
  diffy rendering, entirely read-only.
- **P2 — Read comments.** Fetch + render inline/general threads anchored to rows,
  plus the collapsed "Outdated" section.
- **P3 — Write comments.** Compose from the gutter; reply; edit/delete; batched
  review submit where `pending_review_batch`.
- **P4 — Approvals / merge.** Approve / unapprove / request-changes (per
  `Capabilities`); optional merge with confirm + strategy pick.
- **P5 — More backends.** GitLab, Bitbucket Server/DC, Azure DevOps — implement +
  fixtures + pass conformance.

---

## 11. New `repo_model` / git work

- `Repo::diff_oids(old_sha, new_sha, path)` — sibling of `diff_commit_file`, feeding
  the same pipeline (aggregate diff uses the locally computed `merge-base`).
- Fetch PR refs on demand (libgit2 remote fetch of the provider's ref specs), so the
  commits are local before diffing; degrade to `file_at` fallback otherwise.

---

## 12. Decisions locked

- Diff content is a **local re-diff of fetched commits**, never the server patch.
- Aggregate PR diff defaults to **`merge-base(base,head)..head`** (three-dot).
- Comment anchoring is by **absolute line + side**, provider-translated via
  `DiffContext`.
- **Bitbucket Cloud first, GitHub second** as the abstraction proof.
- UI/navigation bind to the **neutral model + `Capabilities`** only.

## 13. Open questions

- Second proof-of-concept backend: **GitHub** (recommended) vs GitLab / Bitbucket
  Server — confirm before P0.5.
- curl delivery on macOS: all-vcpkg (recommended, reproducible) vs Homebrew.
- Whether `PrCommit` (per-commit narrowing inside a PR) ships in P1 or is deferred.
