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
  http_client.hpp      HttpClient interface (request -> {status,headers,body})
  http_win.cpp         WinHTTP backend      (#if _WIN32)
  http_mac.mm          NSURLSession backend (#if __APPLE__, Obj-C++)
  http_curl.cc         libcurl backend      (Linux / fallback)
  pager.{hpp,cc}       pagination + backoff, backend-agnostic (over HttpClient)
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

struct TextPos { int line; std::optional<int> col; };  // 1-based; col in the LOGICAL file line

struct RangeAnchor {                // the make-or-break abstraction (see §4)
  std::string new_path;
  std::optional<std::string> old_path;   // renames
  Side side;                              // a comment always anchors to ONE side
  TextPos start, end;                     // start==end & no col => whole-line anchor
  DiffContext ctx;                        // SHAs; opaque to the UI
};

struct Comment { std::string id, parent_id, author, body_md, created; };
struct ReviewThread { std::string id; RangeAnchor anchor; bool outdated, resolved;
                      std::vector<Comment> comments; };

struct PrRefs { std::string head_ref, base_ref, head_sha, base_sha; };
```

Reused from `repo_model`: `FileChange` (diffstat) and `CommitInfo` (PR commits).

---

## 4. The anchoring abstraction (why the seam holds)

The UI builds a `RangeAnchor` from the user's **text selection** (see §8a): the
selection's start/end map to `{side, start.line/col, end.line/col}` — a whole-line
click collapses to `start==end` with no col. Each provider merges in the
`DiffContext` it captured when loading the PR, then **clamps the range to its own
granularity** (§5): character-precise, line-range, or single-line. Because we
anchor by **absolute line + side** (not a diff "position"), it survives our local
re-diff choosing different hunk boundaries than the server, and it is the one shape
all backends can consume:

| Backend            | Inline-comment API shape                                            | Granularity it keeps          |
|--------------------|---------------------------------------------------------------------|-------------------------------|
| Bitbucket Cloud    | `inline:{path, to \| from}`                                         | single line (drops end + col) |
| GitHub             | `{path, line, side, start_line?, commit_id}` (+ `ctx.head_sha`)     | line range (drops col)        |
| GitLab             | `position:{…sha…, old/new_path, old/new_line, line_range?}`         | line range (drops col)        |
| Bitbucket Server   | `anchor:{path, line, lineType, fileType}` + text-selection offsets  | char range                    |
| Azure DevOps       | `threadContext:{filePath, right/leftFileStart..End:{line,offset}}`  | char range                    |

**Visual→logical mapping.** Since word-wrap splits a logical line into several
rows, each row carries its logical file line *and its wrap start column*; a
selection's visual `(row,col)` is translated back to logical `(file line, col)`
before it enters the anchor. Line-range backends need only the file line (already
in the row model); the wrap offset matters only for the char-range providers.

**One side only.** A comment anchors to a single side, so selection for commenting
is constrained to one cell (the old *or* new column in side-by-side; the row's own
side in unified). Cross-divider selections are allowed for *copy* but snap to the
dominant side when turned into a comment.

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
enum class CommentGranularity { Line, LineRange, CharRange };

struct Capabilities {
  std::string item_noun = "pull request";  // GitLab: "merge request"
  CommentGranularity granularity;  // BB Cloud=Line, GH/GL=LineRange, BBS/ADO=CharRange
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
`suggestions`, the noun flips to "Merge request" on GitLab. The selection→comment
affordance respects `granularity` too — a multi-line drag offers a range comment
where `LineRange`/`CharRange`, and otherwise snaps to a single line. One UI, N
backends.

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

### 8a. Text selection in the diff (range comments + copy)

The diff rows are custom (per-visual-line `HorizontalLayout`s of coloured `Text`
spans), and Slint `Text` isn't selectable — so selection is implemented over the
row model rather than by a text widget. It's tractable because the view is
**monospace with uniform row height** and we already measure the glyph advance
(the calibration `Text`'s `preferred-width` that feeds `set-wrap-metrics`):

- **Hit-testing.** A pointer drag over the diff maps `(x,y)` → `(row index, col)`
  with `row = floor(y / rowH)` and `col = round((x − pad) / advance)` — exact for
  monospace. Selection is stored in **logical model coordinates**
  `{startRow,startCol,endRow,endCol}` (not pixels), so it survives ListView
  virtualization and scrolling; each visible row draws its slice of the highlight
  behind the spans.
- **Two payoffs.** (1) **Copy** — reconstruct the selected text from the row
  model's spans (a diff viewer wants this regardless of review); reuse the
  existing Win32/`.mm` clipboard helper. (2) **Comment** — an active selection
  raises a "Comment" affordance (gutter button / floating action) that opens the
  composer with the range pre-anchored.
- **Anchor construction.** Rows carry their `side` + logical `old_lineno/new_lineno`
  + wrap start column, so a selection becomes a `RangeAnchor` (§4): visual
  `(row,col)` → logical `(file line, col)`, side from the cell, cols dropped by
  providers that are line-only.
- **Constraints.** Selection for commenting stays within one side/cell; a cross-side
  selection is fine for copy but snaps to the dominant side for a comment. The
  composer only offers a *range* when `granularity != Line`.

**Backend/Slint additions for selection:** a `DiffSelection`
`{start-row,start-col,end-row,end-col,active}` bound to the diff view; callbacks
`copy-selection()` and `comment-selection()` (the latter builds the `RangeAnchor`
in C++ from the row model and opens the composer). No change to the row data model
beyond the wrap-start-column already needed for char-range mapping.

---

## 9. Dependencies (Mac + Windows)

| Concern      | Windows                       | macOS                                   | CMake                                    |
|--------------|-------------------------------|-----------------------------------------|------------------------------------------|
| HTTP         | WinHTTP (`winhttp`)           | NSURLSession (`Foundation`; `.mm`)      | per-OS sources + `target_link_libraries` |
| JSON         | submodule `subprojects/json`  | same                                    | `nlohmann_json::nlohmann_json`           |
| Secret store | `Advapi32` (CredRead/Write)   | `Security.framework` (Keychain; already linked) | per-OS `target_link_libraries`   |

**HTTP is OS-native, not a third-party library.** For HTTPS-only traffic to a few
API hosts, the costly cross-platform concern is TLS trust + proxy, not the HTTP
verbs — and header-only clients (cpp-httplib, mongoose, Beast) all require
bundling OpenSSL/mbedTLS *and* a CA-cert bundle to do TLS, which is exactly the
weight we avoid. WinHTTP + NSURLSession reuse the OS TLS stack, trust store, and
system proxy for **zero dependency** and the smallest binary, behind a tiny
`HttpClient` interface (`request(method,url,headers,body) -> {status,headers,body}`).
libcurl (built against Schannel/SecureTransport, no OpenSSL) is the documented
fallback and the **Linux** backend, dropped in behind the same interface. This
mirrors the existing per-OS split (Win32 helpers in `main.cpp`, APPLE frameworks
in CMake) — and `SecretStore` is per-OS for the same reason.

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
  files; file open via local-first sourcing (fetch refs → `diff_oids`, three-dot);
  **`PrCommit`** — selecting a PR commit narrows the diff to that commit
  (`parent..commit`), analogous to local commit browsing. Also lands the
  **text-selection subsystem (§8a) with copy** — independently useful and the
  prerequisite for range comments.
  *Exit:* pick a hosted repo → open PRs → open one → browse every file with full
  diffy rendering (aggregate or per-commit), select + copy text, entirely
  read-only.
- **P2 — Read comments.** Fetch + render inline/general threads anchored to rows,
  plus the collapsed "Outdated" section.
- **P3 — Write comments.** Compose from a whole-line gutter click *or* from a
  **text-range selection** (§8a) — the composer anchors the `RangeAnchor` and each
  provider clamps to its `granularity`; reply; edit/delete; batched review submit
  where `pending_review_batch`.
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
- Comment anchoring is a **range** (`{side, start, end}`, cols optional) built from
  a **text selection**, provider-translated via `DiffContext` and clamped to each
  backend's `granularity` (line / line-range / char-range).
- **Text selection** in the diff is implemented over the row model using the
  monospace advance + uniform row height (also powers copy); ships in P1.
- **Bitbucket Cloud first, GitHub second** as the abstraction proof.
- UI/navigation bind to the **neutral model + `Capabilities`** only.
- **HTTP is OS-native** (WinHTTP + NSURLSession) behind an `HttpClient` interface;
  no third-party HTTP/TLS dependency. libcurl is the Linux/fallback backend.
- **`PrCommit`** (per-commit narrowing inside a PR) ships in **P1**.

## 13. Open questions

- Residual only: the macOS HTTP backend is Obj-C++ (`.mm`) — confirm the build
  already compiles `.mm` for the GUI target (Slint's Apple path suggests yes;
  verify when P0 scaffolding lands). No design questions outstanding.
