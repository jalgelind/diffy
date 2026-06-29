# GUI-ROADMAP — Diffy Git Client (Slint)

> **Status: implemented.** The plan below has been built end-to-end. CMake is the
> primary build; `libdiffy` (core) / `cli` / `gui` are split; the render model,
> pipeline, `repos.conf` and `[gui]` config exist; the Slint git client builds,
> launches, and renders real repo diffs in both views. Deviations from the
> original plan: (1) config lives as a sub-module *inside* `libdiffy` rather than
> a separate lib, to avoid an output↔config include cycle; (2) the GUI uses its
> own resolved colour palette — reusing the terminal theme's 16-colour names as
> RGB is left as a noted enhancement; (3) the whole UI is one `app-window.slint`
> for simplicity.
>
> **UI iteration done:** real monospace font, auto-select first changed file,
> selected-file highlight + status badges, empty-state, a segmented view toggle,
> **horizontal scrolling** for long lines, and **commit→files browsing** (click a
> commit to list and diff its files vs its parent). Known next steps: true
> word-wrap, a native folder picker (today: paste path + Open), reusing the
> terminal theme colours, and re-introducing row virtualization for very large
> diffs (the horizontal-scroll view is currently non-virtualized).

Plan to grow the project from a single terminal diff tool into **two applications
sharing one diff core**:

1. **`diffy` (CLI)** — the existing terminal tool, unchanged in behaviour.
2. **`diffy-gui`** — a new **Git client** desktop app built on
   [`slint-ui/slint-cpp-template`](https://github.com/slint-ui/slint-cpp-template).
   It opens Git repositories, browses changes/commits, and renders diffs with
   crisp text in both **unified** and **side-by-side** modes.

The GUI is *not* a two-file comparer. It is a repository-oriented client: open a
repo, see its changed/staged/committed files, pick one (or a commit), and view
the diff. The two-file path remains a CLI concern.

---

## 1. Guiding principles

- **One diff core, two frontends.** All algorithm/layout logic lives in a
  backend-agnostic library (`libdiffy`). The CLI serialises it to ANSI; the GUI
  serialises it to Slint. Neither frontend re-implements diffing.
- **No terminal assumptions in the core.** The core must not emit ANSI escapes,
  read `COLUMNS`, or call `isatty`. It produces a *render model* (styled spans);
  serialisation to a medium is the frontend's job.
- **Shared config, additive GUI section.** Both apps read the same `diffy.conf`.
  The GUI adds a `[gui]` table plus a separate `repos.conf`; it never forks the
  format.
- **Git access is a model concern, diffing is ours.** Use `libgit2` to enumerate
  repos/commits/status and fetch blob contents at two revisions. Feed those two
  byte buffers into `libdiffy` — we do the diff, not `git diff`.

---

## 2. Current state (assessment)

Today everything compiles straight into `diffy` and `diffy-test` via `meson.build`
(see `src=[...]`). Notable couplings to break:

| Area | File(s) | Problem for reuse |
|------|---------|-------------------|
| Layout vs. output | `src/output/column_view.cc`, `unified.cc` | Layout (rows, wrap, line numbers) is interleaved with ANSI escape emission. `column_view_render_lines` already split layout from printing (commit `4ff22ef`) — good seam to extend. |
| Color | `src/util/color.{hpp,cc}` | `TermStyle::to_ansi()` is terminal-only, but `TermColor` already resolves `#RRGGBB`/256-palette → RGB, so the *color values* are reusable; only the escape emission is CLI-only. |
| TTY | `src/util/tty.cc` | Terminal capability/width detection — CLI only. |
| Args | `src/diffy_main.cc`, `subprojects/getopt` | `getopt` parsing is CLI only. |
| Config | `src/config/config.cc` | Mixes generic option-sync (reusable) with CLI-specific theme/escape-code precompute. |

Pure/reusable already: `algorithms/*`, `processing/*` (tokenizer, diff_hunk,
diff_hunk_annotate), `util/{readlines,utf8decode,hash,bipolar_array}`.

**Build-system reality:** Slint's C++ story is CMake-first (`find_package(Slint)`
→ FetchContent of `slint-ui/slint`, then `slint_target_sources(tgt ui/*.slint)`,
link `Slint::Slint`). `libgit2`, `fmt`, `GSL`, `crc32c`, `platform_folders`,
`doctest` are all CMake-native. Only the two local subprojects (`getopt`,
`config_parser`) are Meson-only. This pushes us toward CMake (see §4).

---

## 3. Target architecture

```
                ┌───────────────────────────────────────────┐
                │                 libdiffy                    │  (no terminal, no Slint, no git)
                │  algorithms/  processing/  util/  render/   │
                │   • diff algos (myers, patience)            │
                │   • hunk compose + annotate                 │
                │   • render/: AnnotatedHunk → DiffViewModel  │  ← NEW backend-agnostic model
                └───────▲───────────────────────▲─────────────┘
                        │                        │
            ┌───────────┴──────────┐   ┌─────────┴──────────────┐
            │      libdiffy-config  │   │     (consumed by)       │
            │  diffy.conf parsing,  │   │                         │
            │  theme/color resolve  │   │                         │
            └───────▲───────────────┘   │                         │
                    │                    │                         │
        ┌───────────┴───────────┐  ┌─────┴──────────────┐  ┌──────┴───────────┐
        │   cli/  (diffy)        │  │  gui/ (diffy-gui)   │  │  tests/           │
        │  • getopt, tty         │  │  • Slint UI         │  │  • doctest        │
        │  • ANSI serialiser     │  │  • libgit2 repo     │  │  • corpus         │
        │  • model→escape codes  │  │    model            │  │                   │
        └───────────────────────┘  │  • model→Slint model│  └───────────────────┘
                                    │  • [gui] + repos.conf│
                                    └──────────────────────┘
```

**Dependency rules (enforced by CMake target boundaries):**

- `libdiffy` depends on: `fmt`, `GSL`, `crc32c` (hashing), nothing else.
- `libdiffy-config` depends on: `libdiffy`, `config_parser`, `platform_folders`.
- `cli` depends on: `libdiffy`, `libdiffy-config`, `getopt`, terminal utils.
- `gui` depends on: `libdiffy`, `libdiffy-config`, `Slint`, `libgit2`.
- `libdiffy`/`libdiffy-config` must **not** link Slint, libgit2, getopt, or tty.

---

## 4. Build-system strategy

**Decision: migrate the build to CMake** — **DONE.** CMake is now the sole build
system (CLI, GUI, tests, tree-sitter highlighting); Meson has been removed. The
rationale below is kept for context. All build trees live under `out/`.

Rationale: Slint + libgit2 + every third-party submodule are CMake-native;
keeping Meson would mean either bolting a second build system onto the GUI or
fighting Slint's unsupported Meson path. One build system also lets `libdiffy` be
a real consumable target for both apps.

Work involved:
- Top-level `CMakeLists.txt` (`cmake_minimum_required(VERSION 3.21)`) with options
  `DIFFY_BUILD_CLI` (default ON), `DIFFY_BUILD_GUI` (default OFF until ready),
  `DIFFY_BUILD_TESTS`.
- Per-target `CMakeLists.txt` in `libdiffy/`, `config/`, `cli/`, `gui/`, `tests/`.
- Add small `CMakeLists.txt` to the two local subprojects (`getopt`,
  `config_parser`) — both are a handful of files.
- Reuse the existing `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim (already needed for
  crc32c under CMake ≥4; see `meson.build` lines 33-43) for old submodules.
- Bring Slint in exactly as the template does (FetchContent `release/1`,
  `SOURCE_SUBDIR api/cpp`); bring libgit2 via FetchContent or `find_package`.

Fallback if CMake migration stalls: keep Meson for `libdiffy`+CLI, install
`libdiffy` as a static lib + headers, and have the GUI's CMake `find_package` it.
Workable but maintains two build descriptions — not preferred.

---

## 5. Repository layout (target)

```
diffy/
├── CMakeLists.txt                # top-level, options + add_subdirectory
├── libdiffy/                     # the diff core (was src/algorithms,processing,util,output layout)
│   ├── CMakeLists.txt
│   ├── algorithms/               # myers_greedy, myers_linear, patience, algorithm.hpp
│   ├── processing/               # tokenizer, diff_hunk, diff_hunk_annotate
│   ├── util/                     # readlines, utf8decode, hash, bipolar_array, color (values only)
│   └── render/                   # NEW: diff_view_model.{hpp,cc}, layout.{hpp,cc}
├── config/                       # libdiffy-config: diffy.conf parsing + theme resolve
│   ├── CMakeLists.txt
│   └── config.{hpp,cc}, repos.{hpp,cc}
├── cli/                          # diffy binary — CLI-only files
│   ├── CMakeLists.txt
│   ├── diffy_main.cc
│   ├── ansi_serializer.{hpp,cc} # DiffViewModel → ANSI (was column_view.cc/unified.cc output half)
│   └── tty.{hpp,cc}
├── gui/                          # diffy-gui — from slint-cpp-template
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── repo_model.{hpp,cc}   # libgit2 wrapper: repos, status, commits, blobs
│   │   ├── diff_bridge.{hpp,cc}  # DiffViewModel → Slint model
│   │   └── settings_bridge.{hpp,cc} # [gui] + repos.conf <-> UI
│   └── ui/
│       ├── app-window.slint
│       ├── diff_view.slint       # virtualized crisp-text diff (unified + side-by-side)
│       ├── option_bar.slint      # the button strip
│       └── repo_browser.slint    # repo list, status/commit tree
├── tests/
└── subprojects/                  # + libgit2, Slint(fetched); getopt/config_parser get CMakeLists
```

CLI behaviour and command-line surface are preserved; only file locations and the
output half of `column_view`/`unified` move/split.

---

## 6. The modularization refactor (the heart of the work)

Introduce a **backend-agnostic render model** in `libdiffy/render/`. This is what
makes the core reusable and is the single most important deliverable.

```cpp
// libdiffy/render/diff_view_model.hpp
enum class ViewMode { Unified, SideBySide };

enum class SpanStyle {
    Common, Insert, Delete, InsertToken, DeleteToken,
    LineNumber, Frame, Header, EmptyCell, Meta,
};

struct StyledSpan { std::string text; SpanStyle style; };

struct DiffCell {
    std::vector<StyledSpan> spans;
    std::optional<int64_t>  line_number;
    EditType                type;        // Insert/Delete/Common/Meta
};

enum class RowKind { HunkHeader, Content, NoNewlineAtEof };

struct DiffRow {
    RowKind                  kind;
    DiffCell                 left;       // unified: the only cell
    std::optional<DiffCell>  right;      // present only in SideBySide
};

// Split the options by *what they invalidate* — this is the key design point.
// Pipeline options change the diff itself; layout options only change how an
// existing diff is presented. The GUI uses this split so cheap toggles (wrap,
// view mode) never re-run the algorithm on a large file.

struct DiffPipelineOptions {            // re-run compute + annotate when changed
    Algo            algorithm         = Algo::kPatience;
    int64_t         context_lines     = 3;
    EditGranularity granularity       = EditGranularity::Token;
    bool            ignore_whitespace = false;   // affects both compute and annotate
    bool            ignore_line_endings = false; // affects readlines
};

struct DiffLayoutOptions {              // only re-run build_diff_view when changed
    ViewMode        mode              = ViewMode::SideBySide;
    bool            word_wrap         = true;
    bool            show_line_numbers = true;
    int64_t         wrap_width        = 0;        // 0 = no hard wrap (GUI wraps visually)
};

struct DiffViewModel { ViewMode mode; std::vector<DiffRow> rows; };

// Pure: no I/O, no ANSI, no terminal width. Lays out already-annotated hunks.
DiffViewModel
build_diff_view(const DiffInput<Line>& input,
                const std::vector<AnnotatedHunk>& hunks,   // produced under DiffPipelineOptions
                const DiffLayoutOptions& options);
```

The CLI maps its current `ProgramOptions`/`ColumnViewSettings` onto these two
structs; the GUI does the same from `[gui]` + the option bar.

Consequences:
- `cli/ansi_serializer.cc` becomes `DiffViewModel → std::vector<std::string>`
  (resolving `SpanStyle` → `TermStyle` → ANSI via the existing theme). The
  current `column_view_render_lines` already isolates layout; move that layout
  into `build_diff_view` and keep only escape emission in the CLI.
- `gui/src/diff_bridge.cc` becomes `DiffViewModel → slint Model<DiffRow>`,
  resolving `SpanStyle` → RGB color + font attributes from the same theme.
- `unified.cc` similarly splits: hunk→model stays in core, model→text in CLI.

Acceptance: corpus tests (`render_corpus_tests.cc`) still pass byte-for-byte
through the CLI serialiser, proving the refactor is behaviour-preserving.

---

## 7. Configuration & `repos.conf`

### 7.1 Shared `diffy.conf`
Unchanged sections (`[general]`, `[settings]`, `[chars]`, `[style]`, `color_map`)
are read by both apps via `libdiffy-config`. Refactor `config.cc` so the generic
option-sync (`config_apply_options` over an `OptionVector`) is reusable and the
ANSI-escape precompute (`*_escape_codes`) is CLI-only.

### 7.2 New `[gui]` section (in `diffy.conf`)
```
[gui]
default_view = 'side-by-side'   # or 'unified'
theme_variant = 'dark'          # light | dark | system
font_family = 'monospace'
font_size = 13
word_wrap = true
show_line_numbers = true
tab_width = 4
window_width = 1280
window_height = 800
restore_last_repo = true
```
Maps directly onto `DiffViewOptions` defaults + window/font chrome. The GUI writes
this back (preserving comments) using the existing `cfg_serialize` round-trip.

### 7.3 `repos.conf` (new file, GUI-owned)
Stored next to `diffy.conf` in `config_get_directory()`. Remembers opened repos:
```
[[repo]]
path = '/Users/johannes/code/diffy'
name = 'diffy'
last_opened = '2026-06-26T10:15:00Z'
pinned = false

[[repo]]
path = '/Users/johannes/code/other'
name = 'other'
last_opened = '2026-06-20T09:00:00Z'
pinned = true
```
`config/repos.{hpp,cc}` provides `load_repos()`, `add_repo(path)` (de-dupes,
updates timestamp), `remove_repo(path)`, `pin/unpin`, ordered most-recent-first.
Timestamps absolute/UTC.

---

## 8. GUI application design

### 8.1 Bootstrap from the template
Copy the template's `CMakeLists.txt`, `src/main.cpp`, `ui/app-window.slint`
skeleton into `gui/`, rename the target to `diffy-gui`, link `libdiffy`,
`libdiffy-config`, `git2`. Keep the Windows DLL-copy post-build step.

### 8.2 Git model (`repo_model.cc`, libgit2)
Responsibilities (no diffing here):
- Open repo at a path; list branches, HEAD, recent commits (oid, summary, author,
  time).
- Working-tree **status**: untracked/modified/staged/deleted file lists.
- For a selected file, fetch **old** and **new** byte buffers:
  - working-tree change → index/HEAD blob vs. file on disk;
  - staged → HEAD blob vs. index blob;
  - commit → parent blob vs. commit blob.
- Hand the two buffers to `readlines` → `DiffInput` → algorithms → annotate →
  `build_diff_view`.

### 8.3 Screen structure (Slint)
```
┌───────────────────────────────────────────────────────────────────┐
│ Repo switcher ▼   |   branch ▼            [ + Open repo ]           │  app-window
├───────────────┬───────────────────────────────────────────────────┤
│ repo_browser  │  option_bar  [Unified][Side-by-side] [Wrap] [WS]   │  ← button strip
│  • Changes    │              [Tokens][Ctx −3+] [Ln#] [Algo ▼]      │
│  • Staged     ├───────────────────────────────────────────────────┤
│  • Commits    │                                                     │
│   └ files     │   diff_view  (virtualized, crisp monospace)        │
│               │                                                     │
└───────────────┴───────────────────────────────────────────────────┘
```

### 8.4 The button strip (`option_bar.slint`)
A horizontal strip above the diff view. Controls are grouped by cost (see the
pipeline/layout split in §6) so the bridge knows whether to re-diff or just
re-lay-out:

- **Layout-only** (cheap — call `build_diff_view` on the cached annotated hunks):
  - **View mode** toggle: Unified ⇆ Side-by-side.
  - **Word wrap** toggle.
  - **Line numbers** toggle.
- **Pipeline** (re-run compute/annotate for the current file):
  - **Ignore whitespace** toggle.
  - **Granularity** toggle: Token ⇆ Line.
  - **Context lines** stepper (−/+).
  - **Algorithm** dropdown: Patience / Myers-greedy / Myers-linear.

Each control sets a property on a shared `DiffOptions` Slint global; a callback
into C++ either re-lays-out (layout group) or re-runs the pipeline then lays out
(pipeline group), and refreshes the model. Defaults seed from `[gui]`; changes
optionally persist back.

### 8.5 Crisp text rendering
- **Monospace, integer-aligned rows.** One Slint component per `DiffRow`, each a
  horizontal run of `Text` elements colored per `StyledSpan`. Avoid fractional
  positioning; snap row height to an integer line height derived from font
  metrics so glyphs stay sharp at 1× and on HiDPI.
- **Virtualization.** Use Slint `ListView` bound to a `Model<DiffRow>` exposed
  from C++ so only visible rows materialize — required for large files/commits.
- **Side-by-side** = two synchronized `ListView`s (shared vertical scroll) or one
  list of two-cell rows; gutter shows line numbers as a dedicated styled span.
- **Selection/copy** yields the underlying plain text (spans concatenated),
  excluding gutter/line-number spans.
- Backend: default Slint renderer (FemtoVG/Skia) for quality; expose font family
  + size from `[gui]`.

---

## 9. Phased implementation plan

**M0 — CMake parity (no behaviour change).**
Add top-level + per-dir `CMakeLists.txt`; CMakeLists for `getopt`/`config_parser`;
build `diffy` (CLI) and `diffy-test` under CMake. Meson still works. *Done when:*
CLI + all tests build and pass under CMake.

**M1 — Carve out `libdiffy` + `libdiffy-config`.**
Move algorithms/processing/util into `libdiffy/`; move config into `config/`; make
both static-lib targets. CLI links them. *Done when:* `diffy` builds against the
libs, behaviour identical, corpus tests pass.

**M2 — Render model + serialiser split.**
Add `libdiffy/render/diff_view_model.*` and `build_diff_view`; rewrite
`column_view`/`unified` output as `cli/ansi_serializer.cc`. *Done when:* CLI
output is byte-for-byte identical (corpus tests green), and a unit test builds a
`DiffViewModel` directly with no terminal involved.

**M3 — GUI skeleton.**
Drop slint-cpp-template into `gui/`, render a hard-coded `DiffViewModel` (no git
yet) in both unified and side-by-side via `diff_view.slint` + the virtualized
list. *Done when:* `diffy-gui` opens and shows a static diff crisply.

**M4 — Option bar wired.**
`option_bar.slint` + `DiffOptions` global → C++ rebuild. *Done when:* toggles
flip view/wrap/whitespace/granularity/line-numbers/context/algorithm live.

**M5 — Git model.**
`repo_model.cc` over libgit2: open repo, status list, commit list, blob fetch →
`DiffInput`. `repo_browser.slint` tree. *Done when:* open this repo, click a
changed file, see its real diff.

**M6 — Persistence.**
`[gui]` load/save; `repos.conf` load/add/remove/pin; restore last repo + window.
*Done when:* relaunch restores recent repos and view preferences.

**M7 — Polish & retire Meson.**
HiDPI/font tuning, copy/selection, large-file perf pass, keyboard nav; remove
`meson.build` once CMake is the sole build. *Done when:* docs/CI updated, Meson
deleted. — **Meson retired:** `meson.build` deleted, CI/Makefile/docs on CMake
only, all build trees under `out/`. (Polish items remain ongoing.)

---

## 10. Risks & open questions

- **libgit2 vs. shelling out to `git`.** Recommend libgit2 (embeddable, no PATH
  dependency, structured blob access). Shelling out is a fallback if libgit2
  packaging proves painful on a target platform.
- **CMake migration scope.** Two local subprojects need CMakeLists; the old-CMake
  policy shim must cover all submodules. Mitigated by keeping Meson alive through
  M6.
- **Large-diff performance.** Virtualization is mandatory; also consider lazy
  per-file diff computation and capping annotation cost on huge files.
- **Theme reuse.** `SpanStyle → RGB` for the GUI relies on `TermColor` already
  resolving hex/256 → RGB; confirm 16-color SGR names get sensible RGB fallbacks
  for the GUI (terminal-relative colors have no fixed RGB).
- **Config write-back races.** GUI and CLI may both write `diffy.conf`; keep
  writes section-scoped and comment-preserving via `cfg_serialize`.
- **Slint version pin.** Template tracks `release/1`; pin a concrete Slint version
  for reproducible builds.

## 11. Testing

- Keep doctest unit + corpus tests in `libdiffy`/`config` (the bulk move there).
- Add render-model unit tests asserting row/span structure independent of ANSI.
- Keep the CLI byte-for-byte corpus + `tests/testsuite.py` integration test as the
  regression gate for the M2 refactor.
- GUI: a headless smoke test that builds a `DiffViewModel` from two fixtures and
  asserts the Slint model row count/shape; manual UI checklist per milestone.
```
