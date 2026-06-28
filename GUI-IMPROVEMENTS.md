# GUI Improvements Roadmap — diffy-gui

> Forward-looking plan for the Slint git client. The original build-out
> (`GUI-ROADMAP.md`, milestones M1–M7) is done: dual frontends on one core,
> real repo diffs in both views, persistence, lazy commit loading, a resizable
> & persisted layout, keyboard navigation, and a refresh action all exist.
>
> This document covers what comes next — turning a fast **read-only viewer**
> into a comfortable, write-capable client. Items are grouped into phases that
> can ship independently; within a phase, steps are ordered so each builds on
> the last. Effort: **S** ≈ half a day, **M** ≈ 1–2 days, **L** ≈ 3+ days.

Legend: `[ ]` not started · `[~]` in progress · `[x]` done.

---

## Phase 1 — Settings & appearance

Most preferences (font, theme, tab width, view mode) are persisted in `[gui]`
but only editable by hand-editing `diffy.conf`. Surface them in the app.
`Backend.mono-font` / `Backend.font-size` are already **live** properties, so
font changes are cheap; theme is currently built once at startup
(`load_gui_theme`, "no runtime theme switch yet").

- [x] **1a · Settings panel shell** (S) — *done.* a modal overlay opened by a gear
  button (sidebar `REPOSITORY` header). Esc / click-outside closes. No settings
  yet, just the frame + open/close wiring.
  *Done when:* the gear toggles a centered card over a dimmed backdrop.
- [x] **1b · Diff font selection** (S–M) — *done.* family + size controls bound to
  `Backend.mono-font` / `Backend.font-size`, applied live and persisted to
  `gui.font_family` / `gui.font_size`. Picker: a curated per-OS dropdown of
  common monospace families **plus** a free-text field for any installed family
  (Slint can't enumerate system fonts from `.slint`).
  *Done when:* changing the font reflows the diff immediately and survives restart.
- [x] **1c · Tab width + default view** (S) — *done.* a tab-width stepper (triggers a
  re-diff to re-expand tabs) and a default-view (side-by-side / unified) choice,
  persisted to `gui.tab_width` / `gui.default_view`.
- [x] **1d · Live theme switching** (M) — *done.* theme-file dropdown (enumerate the
  themes dir) + dark/light/system variant. Re-run `load_gui_theme` and re-push
  all `Backend` colour props without restart; persist `gui.theme` /
  `gui.theme_variant`. *(Deferred in the first settings-panel pass; this step
  adds the runtime-switch plumbing.)*
- [x] **1e · gui_config round-trip tests** (S) — *done.* unit-test load→save→reload of
  the `[gui]` table including the new keys, guarding the persistence layer.

## Phase 2 — Diff navigation & usability

- [x] **2a · Find-in-diff** (M) — *done.* Cmd/Ctrl-F opens a search field; highlight
  matches in the current diff, Enter / Shift-Enter cycle next/prev and scroll the
  match into view, with an "n of m" count. Esc closes.
- [x] **2b · Jump-to-hunk** (S) — *done.* `n` / `p` move to the next/previous changed
  hunk and scroll it to the top; show a hunk index (e.g. "hunk 3/12"). Builds on
  the existing keyboard-nav `FocusScope`.
- [ ] **2c · Text selection + copy** (M–L) — let the user select diff text and
  copy it (plain, no gutter/markers). The custom span rows don't support native
  selection today; evaluate a selectable-text approach or a "copy file / copy
  hunk / copy selection" affordance as a pragmatic first cut.
- [ ] **2d · Group changes by folder** (M) — a tree/flat toggle for the changes
  list so large change sets are navigable; collapse/expand directories. Keep the
  flat list as the default.

## Phase 3 — Git write actions (viewer → client)

`repo_model` is currently read-only (status, commits, blobs). These add the
write surface that makes it a real client. Each needs careful libgit2 error
handling and a refresh afterward.

- [ ] **3a · Stage / unstage / discard files** (M) — per-file actions in the
  changes list (button/context menu + keyboard); update the index and refresh.
  Discard prompts for confirmation (destructive).
- [ ] **3b · Commit** (M) — a commit box (message + summary/body), author/email
  from git config, commit the staged index, then refresh status and prepend the
  new commit. Include `--amend`.
- [ ] **3c · Stage / unstage individual hunks** (L) — apply a single hunk (or
  selected lines) to the index from the diff view. Highest-value, highest-risk
  write feature; depends on 3a.
- [ ] **3d · Branch list + checkout** (M) — list local (and optionally remote)
  branches, show ahead/behind, switch branches with a dirty-tree guard. Today
  only the current branch name is shown.

## Phase 4 — Repo awareness & freshness

- [ ] **4a · Auto-refresh on focus** (S) — re-scan status when the window regains
  focus (debounced), complementing the manual refresh. Optional file-watcher
  later.
- [ ] **4b · Staged vs unstaged sections** (S–M) — split the changes list into
  "Staged" / "Changes" groups (libgit2 already distinguishes index vs worktree,
  and a per-row "staged" badge already renders — only the grouping is missing),
  so the staging workflow from Phase 3 reads clearly.
- [x] **4c · Remember last file per repo** (S) — *done.* on reopening a repo, reselect
  the file that was open last time instead of the first changed file. *(Was
  scoped out of the earlier UI pass; revisit here.)*
- [ ] **4d · Diff against an arbitrary ref** (M) — pick a base ref/commit to diff
  the working tree or a commit against (not just first-parent), with a small
  ref picker.

## Phase 5 — Performance & robustness

- [x] **5a · Large-file guards** (S–M) — *done.* detect binary / very large files and
  show a placeholder instead of attempting a full token diff; cap annotation
  cost on huge hunks. Surface "binary file", "file too large", "submodule".
- [ ] **5b · Background diff for big files** (M) — compute the diff off the UI
  thread for large blobs (the repo open is already threaded) so selecting a big
  file never blocks the event loop; show a per-file spinner.
- [x] **5c · Virtualization** — *verified done (2026-06).* The diff renders
  through a single virtualized `ListView` (`for data in Backend.rows : DiffRow`)
  with no non-virtualized fallback; the old horizontal-scroll path is gone, and
  the per-cell hybrid-wrap keeps long lines in the same delegate. Re-audit only
  if a new view path is added.

## Phase 6 — Quality & distribution

- [~] **6a · GUI bridge test** (S–M) — *the core is already covered:*
  `libdiffy/render/diff_view_model_tests.cc` asserts `DiffViewModel` row/span
  shape from fixtures with no display. The remaining gap is the GUI conversion
  layer — add a test over `diff_bridge`'s `build_row_model` (DiffViewModel →
  Slint `DiffRowData`: row count, `left/right_cols`, span colours/styles).
- [x] **6b · Automated CI** (M) — *done.* `extras/ci.sh` exists as a local script, but
  there's no automated CI. Add a GitHub Actions job that runs it (checkout with
  submodules, unit/corpus tests) **and** builds `diffy-gui` (caching the
  Slint/cargo build) on at least macOS + Linux.
- [ ] **6c · macOS .app bundle + icon** (M) — package `diffy-gui` as a proper
  `.app` (Info.plist, icon, bundle dylibs) so it launches from Finder; a Linux
  `.desktop` entry as a follow-up.

---

## Suggested sequencing

1. **Phase 1a–1c** — the settings panel you asked for (font, tab width, view),
   smallest and self-contained.
2. **Phase 2a–2b** — find + hunk jump; high daily value, no model changes.
3. **Phase 4a–4b** — focus refresh + staged/unstaged split; sets up staging.
4. **Phase 3a–3b** — stage/unstage + commit; the leap to a real client.
5. Then 1d (live theme), 2c/2d, 3c/3d, Phase 5, Phase 6 as appetite allows.

Dependencies: 3b→3a, 3c→3a, 4b→(clarifies 3a), 1d→1a, 1e→1b/1c.
