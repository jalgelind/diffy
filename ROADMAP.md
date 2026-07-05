# Roadmap

Issues found during the code review of the first-party sources (`src/` and
`subprojects/config_parser/`), plus a plan to close the testing gaps that let
them through.

**How to read this:** the catalog below (bugs + testing work) is a *reference*.
The order to actually do the work in is the **Execution sequence** at the end —
it interleaves tests and fixes so behavior-changing fixes never churn goldens
that were written moments earlier.

Sizes: **S** ≈ <1h · **M** ≈ a few hours · **L** ≈ a day+.

## Progress

- **Stage 1 (bootstrap):** done. Green debug + ASAN baseline (build requires
  `CMAKE_POLICY_VERSION_MINIMUM=3.5` — see Phase 0).
- **Stage 2 (unit tests first):** P2a–P2f **all done** + D1/D2 tokenizer tests.
  New test files: `algorithms/algorithm_tests.cc`, `processing/diff_hunk_tests.cc`,
  `processing/diff_hunk_annotate_tests.cc`, `util/color_tests.cc`, plus additions
  to the config_parser tokenizer tests. Suite is **18 cases / 838 assertions,
  green** (debug; ASAN green as of the color round). Remaining Stage-2: D3
  (serializer round-trip) and D4 (ordered_map) tests.
- **Stage 3 (fixes):** **A1, A5, B1–B4, D1, D2, D5 fixed** (all with tests except
  D5). `diffy` binary rebuilt + smoke-tested. Remaining: D3, D4 (both low-impact —
  see notes), then the behavior-change C-items.
- **Coverage expansion:** added tests for `unified_diff_render`, `readlines`,
  `algo_from_string`, color `to_ansi`/`to_value` round-trips, and — after the
  Phase 1a refactor — the side-by-side renderer (`output/column_view_tests.cc`).
  **Every first-party module now has unit coverage.** Suite is **26 cases /
  893 assertions, green** (debug + ASAN).
- **Stage 4 (Phase 1a) DONE:** `column_view.cc` split so a pure
  `column_view_render_lines(..., width)` returns `vector<string>`;
  `column_view_diff_render` is now a thin width-detect + `puts` wrapper. Proven
  behavior-neutral against a captured golden of `diffy -s` (byte-identical for
  `-s` and `-s -l`).
- **Style:** all new/edited code verified against the repo `.clang-format`
  (clang-format 17, Xcode toolchain), 0 diffs; pre-existing manually-aligned /
  `clang-format off` regions left untouched. Fixed E1 (duplicate keys in
  `.clang-format`).
- **Main-path fixes (this round):** A3 (empty-line UB), A4 (diff exit-code
  convention), E2 (unified stat-failure abort) all fixed and verified on the
  binary; suite **27 cases / 894 assertions** green (debug + ASAN).
- **Bootstrap fixed permanently:** `CMAKE_POLICY_VERSION_MINIMUM` baked into
  the CMake build; `make` works on CMake 4.x with no env var (clean-room verified).
- **Integration test wired:** `make integration-test` runs the `tests/testsuite.py`
  unified-diff → `patch` → matches-B round-trip; passes for all configs/fixtures.
- **C1 (`-W` overload) fixed** — `--no-ignore-whitespace` decoupled from `--width`
  while preserving the intentional short-`-W` overload the testsuite relies on.
- **Real-usage render tests added** — `output/render_corpus_tests.cc` drives both
  renderers over all 65 fixtures and asserts the column view stays aligned
  (equal width) and within the terminal width, and that unified output is
  well-formed. (Caught a measurement subtlety — the header always emits a reset
  code — confirming width must be measured ANSI-stripped, like a terminal does.)
- **Unit-test hole audit** — probed weak spots: found & fixed **E3** (palette
  index overflow/junk in `parse_string`); added coverage proving `color_map`
  aliasing, full attribute round-trip, Myers greedy/linear edit-distance equality,
  and `is_empty`/`is_whitespace` all behave correctly. Documented the two biggest
  remaining holes (`config.cc` is filesystem-bound; `to_value` can't represent
  non-palette colors). Suite now **34 cases / 1344 assertions**, green (debug +
  ASAN).
- **Remaining:** A2 (git-difftool perms — niche, needs env+fs to test);
  C3/C4 underflow hardening; behavior-change C2 (`right_trim`); D3/D4
  (low-impact config_parser); P4a (sanitized binary sweep); CI workflow. Full
  `parse_args` extraction deferred (overload must stay; verified via binary).
- Patience concern resolved (not a correctness bug — see below).

---

## Guiding principles

1. **Test before fix.** Every bug fix ships with a test that fails before and
   passes after. For correctness bugs the test asserts the *correct* value; for
   behavior changes (exit codes, `right_trim`) the test *encodes the decision*.
2. **Invariants for the diff layer, goldens for the render layer.** A diff is
   verifiable without a golden: applying the edit script to A must reproduce B.
   That is an exact, stable oracle — prefer it over brittle expected-output
   strings. Rendering has no such invariant (the output *is* the spec), so use
   snapshots there.
3. **Separate pure logic from I/O and globals.** Most untestable code is
   untestable because layout, ANSI styling, tty/clock/env access, and `puts()`
   are fused. Split them; test the pure half.
4. **Never touch the real user environment in tests.** Sandbox the config dir,
   inject width/timestamps, and reset global color state.

---

## Bug catalog

### Batch A — Clear bugs (high confidence)

- [x] **A1 · Background color never parsed for bg-only styles** — FIXED
  (`color.cc:301`, guard now on `"bg"`). Regression test in `color_tests.cc`
  ("bg-only table applies the background"). Green.

- [ ] **A2 · git difftool "changed file" permissions wrong** (S) —
  `src/diffy_main.cc:350-355`. `right_file_permissions` is read from
  `opts.right_file_name` *before* it is assigned. Read permissions from the
  actual content files; use `git_base` only for display names:
  ```cpp
  opts.left_file_permissions  = read_file_permissions(opts.left_file);
  opts.left_file_name         = git_base;
  opts.right_file_permissions = read_file_permissions(opts.right_file);
  opts.right_file_name        = git_base;
  ```
  Also drop the redundant double-assign in the "added file" branch (347/349).
  *Regression test:* requires the `parse_args` extraction (P1b) to be reachable;
  assert names/permission sources for the added/deleted/changed cases.

- [x] **A3 · UB on empty last line** — FIXED (`diffy_main.cc`). Guarded the inner
  `.back()` (`right_line_data.back().line` can be empty after `-i` trims a
  newline-only line). Verified ASAN-clean via `diffy -u -i <file> <newline-only>`.

- [x] **A4 · Inconsistent exit codes** — FIXED (`diffy_main.cc`). Both renderers
  now follow `diff`'s convention: `0` = identical, `1` = differences, `2` = error
  (`exit_code = hunks.empty() ? 0 : 1`; error paths return 2). Verified the full
  matrix on the binary (unified/column × identical/different = 0/1, missing = 2).
  **Integration interaction handled:** `tests/testsuite.py`'s (disabled) crash
  check used `return_code != 0`, which conflicts with exit-code-1-on-difference;
  changed it to flag only real crashes (`< 0`) or errors (`== 2`). The patch
  round-trip (which ignores diffy's exit code) still passes for all configs.

- [x] **A5 · Uncaught `bad_variant_access` crash on hand-edited theme** — FIXED
  (`color.cc:286`). `TermStyle::parse_value` now guards `attr` with
  `contains("attr") && is_array()` and skips non-string elements, so a theme that
  omits `attr` or mistypes it no longer throws. Root cause: `OrderedMap::operator[]`
  inserts a default `Value` (whose variant defaults to `Table`) for a missing key,
  and `.as_array()` on it threw. Regression tests in `color_tests.cc` ("missing
  attr does not crash", "attr of the wrong type does not crash"). Green.

### Batch B — Cleanups (cosmetic, no behavior risk) — DONE

- [x] **B1** Help text now reads "new-file (right)" (`diffy_main.cc`). Verified in
  `--help` output.
- [x] **B2** Removed dead `if (0)` debug blocks (`diffy_main.cc`).
- [x] **B3** Removed stray empty statement `;` (`diff_hunk_annotate.cc`).
- [x] **B4** Simplified `parse_hex` `#RGB` to `return TermColor(..., r, g, b)`
  (`color.cc`); pinned equivalent by the `color_tests.cc` "#RGB" case.

### Batch C — Semantic / heavier (opt in per item)

- [x] **C1 · `-W`/`--width`/`--no-ignore-whitespace` overload** — FIXED (minimal,
  safe). **Finding:** the short `-W` overload (numeric → width, else →
  no-ignore-whitespace via the `optind--` hack) is *intentional and load-bearing*
  — `tests/testsuite.py` runs `-I -W -a mg` to neutralize user config, and the
  commit history confirms it. So the overload can't simply be removed. Fixed the
  real fragility instead: gave `--no-ignore-whitespace` its own long-only option
  code (`256`) so it no longer shares `'W'` with `--width`, dropped the redundant
  trailing `W` from the short-option string, and made a value-less `--width` a
  no-op (was wrongly toggling whitespace). Verified on the binary: `--width=30`
  == `-W 30`, `--no-ignore-whitespace` parses, and the testsuite `-I -W -a`
  pattern still parses to 2 positionals; integration patch round-trip still green.
  (Full `parse_args` extraction into a unit-testable module deferred — high risk
  for marginal benefit given the overload must stay; verified via the binary +
  `make integration-test` instead.)
- [ ] **C2 · `right_trim` over-trims** (S, **behavior change**) —
  `src/util/readlines.cc:90`. With `ignore_line_endings` it strips *all* trailing
  whitespace, not just `\r\n`. **Note the depth:** the checksum is computed
  *after* trimming, so this changes hashes → changes diff results for files
  differing only in trailing whitespace. *Test:* with `-i`, lines differing only
  by trailing whitespace hash equal and render as Common.
- [ ] **C3 · Unsigned-underflow hardening** (M) — make loop bounds explicit so
  they don't rely on wraparound / non-empty invariants:
  - `insert_alignment_rows` — `src/output/column_view.cc:243` (`ia -= 2`)
  - `extend_hunk_ranges` — `src/processing/diff_hunk.cc:64` (`i -= 1` on `size_t`)
  - `annotate_tokens` tail loop — `src/processing/diff_hunk_annotate.cc:128`
    (`size() - 1` when empty)
  Each gets a test that exercises the boundary (empty/first-row/single-element).
- [ ] **C4** `column_view.cc:627` uses `std::max(left.size(), right.size())` then
  indexes both columns; safe only via an earlier equal-size `assert`. Make the
  invariant explicit (S).

### Batch D — Newly reviewed: util + config_parser subproject

Found in a second review pass (`util/tty.cc`, `util/hash.cc`, and the
`config_parser` subproject). All verified by direct reading.

- [x] **D1 · Float literals never tokenized** — FIXED (`config_tokenizer.cc`).
  Float detection now uses `strtof` (libc++ lacks reliable `from_chars<float>`)
  and requires the whole token to parse. Regression test "float literal is tagged
  Float" in `config_tokenizer_tests.cc`. Green. (Note: `TokenId_Float` is not in
  `TokenId_MetaValue`, so the parser still ignores floats — this fix is for
  tokenizer correctness, not a new feature.)

- [x] **D2 · Integer tokenization accepts partial / out-of-range** — FIXED
  (`config_tokenizer.cc`). Now requires `int_result.ec == std::errc{} &&
  int_result.ptr == end`, so `"12abc"` and out-of-range numbers are no longer
  integers. Regression test "digits followed by letters are an identifier". Green.

- [ ] **D3 · Serialized strings are not escaped** (S–M) —
  `config_serializer.cc:107`: `fmt::format("'{}'", value.as_string())`. A value
  containing `'`, `\`, or a newline produces output that won't round-trip / can
  break re-parsing. Low impact for diffy's own data (color names, glyphs), but a
  real round-trip bug. *Test:* serialize→parse round-trip for a string with a
  quote.

- [ ] **D4 · `OrderedMap::insert` keeps the first value on duplicate key** (S) —
  `ordered_map.hpp` (~line 43): uses `std::map::insert` (no overwrite), unlike
  `operator[]`. A config with a duplicate key silently keeps the first, not the
  last. Decide intended semantics and make the two paths consistent.

- [x] **D5 · `tty_get_capabilities` switch fall-through** — FIXED (`tty.cc`).
  Added `break;` after `case 16:` (16-color terminals are no longer mislabeled
  256-color) and changed the `term_fd` cleanup guard to `>= 0`. (No unit test —
  the path depends on `popen`/`isatty`; covered by the smoke run.)

- [ ] **D6 · Lines/tokens compared by 32-bit hash only** (design note, M) —
  `Line::operator==` / `Token` equality compare `crc32c` checksums with no
  content fallback (`util/readlines.hpp:28`, `processing/tokenizer.hpp`). A hash
  collision silently produces a wrong diff. Consider a content compare on hash
  match (cheap: only on equality), at least for the line level. Document the
  trade-off if left as-is.

### Needs investigation before fixing

- [ ] **config_parser robustness on malformed input** (M, uncertain) — the parse
  state machine has paths that may misbehave on crafted/truncated input:
  `scope_stack.top()` with a possibly-empty stack for a bare top-level scalar
  (`config_parser.cc:419`), `PARSER_NEXT_TOKEN` advancing past the terminator
  while `in_critical_section` with no bounds check, and a `PopScope`→`Finish`
  fall-through (missing `break`, ~`:566`). Not confirmed as live bugs. *Resolve
  by fuzzing / targeted edge tests* (empty input, unbalanced brackets, bare
  scalar, trailing comma) rather than by inspection.

- [x] **Patience "dropped match" — RESOLVED: not a correctness bug** (quality/perf)
  — `src/algorithms/patience.hpp:95-100`. Confirmed by analysis + the new tests:
  `patience_sort` never starts a new pile when an element exceeds all stack tops
  (`found == end() && !empty`), so it under-counts the LIS for increasing runs
  and picks a *suboptimal* anchor set. But `do_diff` falls back to `MyersLinear`
  for every region without anchors, and Myers is optimal — so the output is
  always a valid, minimal diff. The reconstruct invariant passes for patience
  across all 65 fixture pairs + synthetic cases. **Net effect:** patience
  degenerates toward Myers (more fallback work on large files, less
  unique-line-anchored structure) — a performance/style issue that lines up with
  the open TODO "Compare patience with `git diff --patience`". Fixing
  `patience_sort` to append a new pile in that branch would restore intended
  anchoring; tracked as an optimization, not a bug.

### Batch E — Found during coverage work

- [x] **E1 · `.clang-format` had two duplicate keys** — FIXED.
  `PenaltyReturnTypeOnItsOwnLine` and `SpacesInContainerLiterals` each appeared
  twice; clang-format 17 refuses to load such a config. Removed the first
  occurrence of each (kept the last-wins value). Formatting/CI can now run.
- [x] **E2 · `unified` asserts on stat failure** — FIXED (`unified.cc`). Replaced
  the `assert(0)` with a plain `return false`, so a missing/unreadable name yields
  empty output instead of a debug abort. Unit test "missing file name yields empty
  output" re-added in `unified_tests.cc`.

- [x] **E3 · `parse_string` palette index unchecked** — FIXED (`color.cc`). Found
  via a unit-test *hole audit*: `parse_string("P300")` silently truncated to
  palette 44 (`uint8_t` overflow), and `"P12x"` parsed as 12 (trailing junk
  ignored) — the color analog of D2. Now requires the whole suffix to parse and
  `0 <= index <= 255`, else nullopt. Regression tests in `color_tests.cc`.

### Hole audit — coverage added, no bug (verified the behavior is correct)

- [x] **`color_map_set` aliasing** — the shipped "arbitrary color aliases" feature
  was untested; added a test (`color_tests.cc`): an alias set via `color_map_set`
  resolves through `parse_string`. Works.
- [x] **Attribute encode/decode round-trip** — only `bold` was tested; added a
  test covering all eight attributes through `to_value`/`parse_value`. The enum's
  bit-3 gap is harmless; all round-trip intact.
- [x] **Myers optimality differential** — the reconstruct invariant only checks
  *validity*, so a regression producing valid-but-bloated diffs would slip
  through. Added a test asserting MyersGreedy and MyersLinear agree on edit
  distance (both are optimal; patience excluded by design).
- [x] **`is_empty` / `is_whitespace`** — used by the ignore-whitespace path, were
  untested; added direct tests.

### Known remaining holes (documented, not yet closed)

- [ ] **`config.cc` (`config_apply_options` / `config_apply_theme`) untested** —
  the biggest gap; this is where A1/A5 lived. It reads/writes the real config dir
  (`sago::getConfigHome()`, not XDG on macOS) and its helpers are file-local, so
  unit testing needs a config-dir seam (Phase 1d) or a refactor. High value if a
  sandboxing approach is found.
- [ ] **`TermStyle::to_value` can't represent non-palette colors** (latent) —
  `get_term_color_name` returns `"default"` for any 24-bit/8-bit color, so
  `to_value` of a hex/palette color round-trips to `default`. Not triggered in
  normal flow (user values are preserved as raw strings in the Value tree; only
  *defaults*, which are palette colors, go through `to_value`), but a real
  limitation if any path ever re-serializes a styled color. Add a serializer that
  emits `#RRGGBB`/`P<n>` for non-named colors.

### Platform / low priority

- [ ] Windows `getdelim` reads `getc()` into a `char` — `src/util/readlines.cc:41`
  (classic EOF/0xFF truncation). Windows-only; defer.

---

## Testing strategy

### Current state

- **Unit tests (doctest)** cover only `tokenizer`, `bipolar_array`, `utf8decode`.
  `config_parser` ships its own tests via `config_parser_test_dep`.
- **No unit tests** for the three diff algorithms, `compose_hunks`,
  `annotate_hunks`, either renderer, or color/config parsing.
- The `tests/test_cases/` `_a`/`_b` pairs (from `extras/git-extract-test-cases`)
  are **not wired into any runner** — manual + `make sanitize-address` crash
  smoke only. No golden comparison, no CI.

### Phase 0 — Bootstrap & baseline (blocks everything) · M

- [x] `git submodule update --init` — done; all five `subprojects/*` checked out
  (GSL v4.0.0, crc32c 1.1.2, doctest v2.4.9, fmt 9.0.0, platform_folders 4.2.0).
- [x] **Bootstrap blocker — FIXED in the CMake build.** CMake >= 4 dropped
  compatibility with `cmake_minimum_required(VERSION <3.5)`, which the pinned
  `crc32c` *and* `GSL` (its `tests/`) subprojects declare. Now passing
  `CMAKE_POLICY_VERSION_MINIMUM=3.5` to the crc32c/fmt/GSL/platform_folders
  subprojects (and `GSL_TEST=OFF` to skip building googletest).
  Verified with a clean-room `cmake` configure + build + `diffy-test` **without**
  the env var — all green. No more env-var dance for `make`.
- [x] `make test` green — debug build + `diffy-test`: **8 cases / 499 assertions,
  0 failed**. (Build log confirms the `config_parser` tests already link into
  `diffy-test` via `config_parser_test_dep`.)
- [x] `make sanitize-address` green — `diffy-test` under ASAN: 499 assertions,
  0 failed, no sanitizer reports. Baseline established.
- [ ] Optional coverage: add `-Db_coverage=true` option; `gcovr` is not installed
  (install it, or use `llvm-cov gcov`). Use only to *find* gaps, not as a gate.
- [ ] Add three tiny test helpers (header-only, test-side):
  `lines("abc")` → `vector<Line>` (one Line/char, `checksum = char`);
  `lines_from_file(path)`; `reconstruct_b(A, edit_sequence)` for the invariant.

### Phase 1 — Refactors for testability (no behavior change)

- [x] **P1a · Column view: split layout from I/O** — DONE. Extracted
  `column_view_render_lines(diff_input, hunks, config, options, width)` returning
  one styled string per visual row; `column_view_diff_render` is now a thin
  width-detect + `puts` wrapper. Verified byte-identical to a captured golden of
  `diffy -s` / `diffy -s -l`. (A `strip_ansi` helper turned out unnecessary — a
  default `ColumnViewState` has empty style escape codes, so test output is
  ANSI-free.)
- [ ] **P1b · Extract arg parsing.** Pull the `parse_args` lambda out of `main`
  into a pure function returning `{ProgramOptions, Action, error_string}` instead
  of calling `exit()`/`show_help` inline. Unlocks tests for A2, A4, C1. (M)
- [ ] **P1c · Inject non-determinism.** Unified renderer: pass the timestamp in
  (it currently `stat()`s the display name and formats local mtime+nanos — not
  reproducible). Make tty width/caps injectable. (S–M)
- [ ] **P1d · Sandbox config.** Ensure config load/save honors an overridable
  config dir (e.g. `XDG_CONFIG_HOME`) so tests never read/write the real one, and
  expose a reset for the `kColorTable` global in `color.cc`. (S)

### Phase 2 — Unit tests for core logic (no refactor needed)

- [x] **P2a · Diff-algorithm invariant over the whole fixture corpus** — DONE
  (`src/algorithms/algorithm_tests.cc`). Reconstruct invariant for all three
  algorithms; runs over all **65** fixture pairs (`DIFFY_TEST_CASES_DIR` injected
  by the build). MyersGreedy is capped to pairs ≤ 3000 lines (its `O(D·(N+M))`
  memory), logged in the test output; MyersLinear + Patience run on all. Green
  under both the debug and ASAN builds.
- [x] **P2b · Algorithm edge & boundary cases** — DONE. empty/empty,
  empty/non-empty, identical, single-sub, prepend/append, full-replace, and the
  **index-type boundaries** 126/127/128 and 32766/32767/32768 (change + append).
  `bug_mg_uint8` is exercised through the corpus test (one of the two pairs that
  run MyersGreedy).
- [x] **P2c · Patience trailing-match** — DONE / resolved (see "Patience" above):
  not a correctness bug. Characterization test asserts the prepend case stays
  valid + minimal (5 common, 1 insert).
- [x] **P2d · `compose_hunks`** — DONE (`src/processing/diff_hunk_tests.cc`):
  empty sequence (no underflow), all-common, exact from/to indices for a single
  substitution, the `context_size*2+2` merge rule (2 hunks at ctx 3, 1 at ctx 4),
  change at start/end, and a partition/coverage check. Green.
- [x] **P2e · `annotate_hunks`** — DONE (`src/processing/diff_hunk_annotate_tests.cc`):
  segments tile each line fully/contiguously, EditLine counts match
  `from_count`/`to_count`, token granularity keeps shared tokens Common, and
  `ignore_whitespace` marks whitespace segments Common. Green.
- [x] **P2f · Color/config parsing** — DONE (`src/util/color_tests.cc`):
  `parse_string`/`parse_hex` (`#RGB`, `#RRGGBB`, `P<n>`, junk) and
  `TermStyle::parse_value` (bg-only → A1 regression, missing/mistyped attr → A5
  regression, attr decode). Green. (Round-trips `to_value`→`parse_value` still
  TODO; fold in when fixing B4.)

### Phase 3 — Side-by-side / column-view snapshots (after P1a)

Inline raw-string expectations in doctest (reviewable in-diff; no file I/O).
Fixed input + fixed theme + injected width.

- [x] **P3 (core) DONE** — `output/column_view_tests.cc`: basic layout (header
  carries both names, every row has the column separator, both sides' content
  present, exact row count), word-wrap-produces-more-rows-than-chop, and
  narrow-width clamp (no crash). Default `ColumnViewState` → ANSI-free output, so
  assertions are direct.
- [x] **P3 (real-usage corpus rendering) DONE** — `output/render_corpus_tests.cc`
  drives *both* renderers over all 65 fixtures (the same path as `diffy -s/-u
  file_a file_b`) and asserts: column-view rows are all the **same visible width**
  and **never exceed the terminal width** (ANSI-stripped — see note below), and
  unified output is well-formed (`--- `/`+++ ` headers, then only `@`/` `/`+`/`-`
  lines). Exercises real wrapping (icdiff_128 has 780-char lines), tabs, and
  multi-byte glyphs; green under debug + ASAN. **Test note:** width must be
  measured *after* stripping ANSI — `make_header_columns` bakes a trailing `\033[0m`
  into the header even when styling is empty (harmless, but the test learned it).
- [ ] **P3 (extensions, optional):** `insert_alignment_rows`
  delete-only/insert-only/interleaved cases (the `ia -= 2` path; would gate C3),
  `color_code_file_permissions` (pure), and full-ANSI snapshots with a fixed
  theme. The corpus test above already covers wrap/chop + multi-byte broadly.

### Phase 4 — Automate the corpus + CI

- [x] **P4 (patch round-trip) wired** — `make integration-test` builds `diffy`
  and runs `tests/testsuite.py`, which diffs every fixture pair, applies the
  unified output with `patch`, and checks the result matches B (sha1). Passes for
  all 6 algorithm/context configs (mg0/1, ml0/1, p0/1) across all fixture groups.
  This is the binary-level analog of the P2a reconstruct invariant.
- [ ] **P4a · Sanitized corpus runner (still open)** — also run the fixture
  sweep under the ASAN/UBSAN build and fail on crash. (Library-level P2a already
  runs under ASAN; this would cover the shipped binary.) (S)
- [ ] **P4b · Renderer golden regression** — store reviewed expected output for a
  curated subset; `--update-goldens` toggle to refresh. Goldens only for
  rendering; the algorithm layer stays on the invariant. (M)
- [ ] **P4c · CI** (none today) — GitHub Actions: checkout with submodules, then
  `make test` + `make sanitize-address` + corpus runner, on push/PR. Linux
  first; Windows later (also exercises the `getdelim` path). (M)

### Housekeeping

- [x] Confirmed the `config_parser` tests execute in `diffy-test` (build log lists
  them, and the new tokenizer tests run there).
- [ ] Minor: the `readlines`/`unified` tests write fixtures into the system temp
  dir (`temp_directory_path()`) and don't clean them up. Harmless (system temp,
  fixed names), but a TestFixture/RAII cleanup would be tidier if revisited.

### Pre-commit state (reviewed)

Working tree matches this roadmap: 10 modified files (the A1/A5/B/D1/D2/D5/E1
fixes + the P1a refactor) and 9 new files (8 test files + `ROADMAP.md`). Source
diffs reviewed — clean, no leftover debug, comments match repo style. Suite green
at **26 cases / 893 assertions** (debug + ASAN). Nothing committed yet; suggested
commit split is in the chat. Recommend branching off `master` before committing.

---

## Execution sequence (recommended) — unit tests first

Unit tests lead. The only thing ahead of them is the bootstrap that makes the
project compile at all; everything after them is fixes, then the refactors that
unlock the *remaining* unit tests, then snapshots, then integration/CI last.

Some unit tests in Stage 2 assert *correct* behavior and therefore **fail until
the matching fix lands in Stage 3** — that is intended (red → green). Keep them
in but expect a known-red window.

**Stage 1 — Bootstrap (precondition, blocks all)**
1. **Phase 0** — `git submodule update --init`, green `make test` +
   `make sanitize-address` baseline, the three test helpers, config-dir sandbox
   + `kColorTable` reset.

**Stage 2 — Unit tests first (everything testable without a refactor)**
2. **P2a** — corpus reconstruct-B invariant across all three algorithms (also
   characterizes current behavior as a safety net before any change).
3. **P2b, P2c** — algorithm edge/boundary tests; pin `bug_mg_uint8`; resolve the
   patience question via the invariant + Myers differential.
4. **P2d, P2e** — `compose_hunks` + `annotate_hunks`.
5. **P2f** — color/config parsing (asserts the *correct* bg behavior → red until A1).
6. **config_parser unit tests** — number tokenization (D1, D2), serializer
   round-trip (D3), `OrderedMap` duplicate-key semantics (D4); add edge/fuzz
   cases for the malformed-input concerns. Wire these into `diffy-test`.

**Stage 3 — Fixes covered by Stage-2 tests (turn the red tests green)**
7. **A1, B4** (color), **D1, D2** (tokenizer numbers), **D3** (serializer escape),
   **D4** (ordered_map), **D5** (tty fall-through), and **Batch B** cosmetics.
   Each already has a test from Stage 2.

**Stage 4 — Refactors to unlock the remaining unit tests** (guarded by P2a +
throwaway characterization goldens)
8. **P1a** — column view → `vector<string>`.
9. **P1b** — extract `parse_args` into an `exit()`-free pure function.
10. **P1c, P1d** — inject timestamp/width/tty caps; sandbox config (if not already
    done in Phase 0).

**Stage 5 — Unit tests enabled by the refactor, then their fixes**
11. Arg-parsing tests → fix **A2, A3, A4, C1**.
12. Renderer row tests for the underflow paths → fix **C3, C4**.

**Stage 6 — Side-by-side snapshots**
13. **P3a, P3b, P3c**.

**Stage 7 — Integration + CI (last)**
14. **Phase 4** — sanitized corpus runner, renderer golden regression, CI.

**Stage 8 — Remaining behavior change + housekeeping**
15. **C2** (`right_trim`) with its hash-dependency test; **D6** decision.

Defer: Windows `getdelim`; config_parser malformed-input hardening until fuzzing
confirms a live bug.

---

# Performance & memory roadmap (large diffs)

Evidence-based: measured on a release build with `/usr/bin/time -l`. Inputs are
synthetic — `bigN` = N lines with ~half the lines changed (scattered, large edit
distance); `tail` = 20000 lines with a single change at the top (one big
contiguous common region).

## Measured baseline (peak resident set size · wall time)

| input         | MyersGreedy   | MyersLinear  | Patience (default) |
|---------------|---------------|--------------|--------------------|
| 1000, ½-churn | 10.6 MB       | 2.9 MB       | 8.4 MB             |
| 2000, ½-churn | 34.5 MB       | 3.6 MB       | 24.8 MB            |
| 4000, ½-churn | 129.6 MB      | 4.5 MB       | 87.5 MB · 0.41s    |
| 20000, 1-line | 8.3 MB        | 7.6 MB       | 9.8 MB · 0.00s     |

Reading: MyersLinear is flat (linear space). MyersGreedy and **Patience** both
grow ~quadratically with churn. The 20k contiguous-common case is cheap for all
(a big common *block* is consumed by `adjust_head` in one pass) — the blow-up is
specific to **scattered** common lines, which is the common real-world shape.

## Findings (ranked)

- [x] **PERF-1 · `patience_sort` broken LIS → Patience was O(N²)** — FIXED
  (`patience.hpp`). Replaced with a standard patience-sort LIS (binary-search the
  pile to replace its top, append a new pile when a card exceeds all tops, recover
  via back-pointers). The old code dropped any card exceeding all pile tops, so it
  kept only the first anchor and `do_diff` recursed ~N deep rebuilding maps.
  **Result (4000/½):** 87.7 MB / 0.42s → 4.1 MB / ~0s; now O(N) like MyersLinear.
  Largest single win. Verified by the reconstruct invariant + patch round-trip.

- [x] **PERF-2 · MyersGreedy O(D·(N+M)) memory → OOM** — FIXED (`myers_greedy.hpp`).
  `do_edit_distance` now bails (returns -2) once the snapshot trace would exceed a
  256 MB budget, and `diff_impl` falls back to linear-space `MyersLinear`.
  **Result (16000/½):** ~2 GB (would OOM) → bounded ~273 MB. Default
  (Patience→MyersLinear) was already linear-space.

- [x] **PERF-3 · `adjust_tail` was O(n²)** — FIXED. `push_back` + one
  `std::reverse` instead of insert-at-front.

- [x] **PERF-4 · `do_diff` copied through intermediate vectors** — FIXED. Now an
  accumulator (`do_diff(slice, std::vector<Edit>& out)`) appends in place; the
  MyersLinear fallback and `head` append directly too.

- [x] **PERF-5 · Missing `reserve()`** — FIXED. `insertion_points` (compose_hunks),
  the `records` map and `piles` (patience) now reserve up front.

- [x] **PERF-6 · `Edit`/`EditIndex` were fat** — FIXED (`algorithm.hpp`).
  `EditType` is now `: uint8_t` and `EditIndex::value` is `int32_t` (indices fit;
  a >2e9-line file is unrealistic), shrinking `Edit` ~40 B → ~24 B. Verified by
  the reconstruct + boundary tests (exercise up to 32768 lines). Small relative to
  total RSS, so it does not move the table, but it halves `edit_sequence` memory.

- [~] **PERF-7 · `annotate_tokens` per-hunk serial — DEFERRED (not a bottleneck).**
  Measured column view (`-s`, which runs `annotate_tokens`) on 16000 lines /
  half-changed: **0.10s / 96 MB**. Threading it would add complexity and race risk
  for a path that is already sub-0.1s; revisit only if profiling later shows it
  matters.

## Results (release build, `/usr/bin/time -l`, ½-churn synthetic inputs)

Patience (the default) — the headline:

| lines | BEFORE          | AFTER           |
|-------|-----------------|-----------------|
| 1000  | 8.5 MB          | 2.7 MB          |
| 2000  | 24.8 MB         | 3.2 MB          |
| 4000  | 87.7 MB / 0.42s | 4.1 MB / ~0s    |
| 8000  | 336 MB / 1.75s  | 5.8 MB / 0.01s  |
| 16000 | ~1.4 GB (O(N²)) | 9.1 MB / 0.02s  |

MyersGreedy (`-a mg`) — PERF-2 OOM guard:

| lines | BEFORE             | AFTER (guarded) |
|-------|--------------------|-----------------|
| 4000  | 129.3 MB           | 129.3 MB        |
| 8000  | ~520 MB            | 268.8 MB        |
| 16000 | ~2 GB (OOM)        | 272.6 MB        |

MyersLinear was already linear (2.9→8.1 MB) and is unchanged. All correctness
gates green throughout (unit 34/1344 debug + ASAN, integration patch round-trip).

## Streaming column-view output (PERF-8) — done, with caveats

Refactored `column_view` to render and emit **one hunk at a time**
(`column_view_render_streaming` + `emit_columns`); `column_view_diff_render`
streams rows straight to `puts` (the test entry collects into a vector).
Byte-identical to the pre-refactor golden; unit + ASAN + integration green.

**Honest result:** this was a smaller win than projected, and the earlier
"~500 MB worst case" prediction was wrong. Measured `-s` peak RSS at 100k lines:

| change pattern        | hunks   | peak RSS |
|-----------------------|---------|----------|
| 1% scattered          | ~1000   | 54 → 42.5 MB |
| 1-per-40 scattered    | ~2500   | 51 MB    |
| 50% (one merged hunk) | **1**   | 495 → 491 MB (no change) |

Why: streaming only bounds memory across *hunks*. At 50% churn the changes are
within each other's context window, so `compose_hunks` merges everything into a
**single giant hunk** — "one hunk at a time" is then the whole diff. Streaming
does eliminate the materialized all-rows buffer (the ~10–20% drop in the
multi-hunk cases) and starts output immediately for huge diffs (better
time-to-first-line through a pager), but it cannot subdivide one hunk.

- [x] **PERF-9 · `string_view` segments — ATTEMPTED, REVERTED (not worth it).**
  Implemented `DisplayLineSegment::text` as a `std::string_view` viewing either the
  source line or a render-scoped glyph pool (`std::deque<std::string>`, stable
  addresses) for generated tab/space/etc. glyphs; switched the `utf8_*` helpers to
  `string_view`. It was correct — byte-identical golden, ASAN-clean over all 65
  fixtures, tabs/spaces render aligned. **But the worst-case win was only ~4%
  (491 → 472 MB):** the saved bytes were just the smaller segment struct (48 → 32 B);
  the 472 MB is dominated elsewhere (the `DisplayLine`/segment-vector overhead and
  the annotated-hunk input), which `string_view` doesn't touch. Reverted — the
  glyph-pool lifetime complexity wasn't justified by 4% on an unrealistic case.
  If this is ever revisited, the lever is the `DisplayLine`/`std::vector<segment>`
  overhead per displayed line, not the segment text. (Trivial separable win noted:
  `transform_edit_line` takes `ColumnViewState` **by value**, copying the whole
  config per line — change to `const&`.)

---

# Theme / inverted color-scheme roadmap

Goal: make a **fully inverted** theme — one that fills backgrounds rather than
only recoloring text (e.g. a light-on-dark or GitHub-style block-highlight
scheme) — render *cleanly*: every visible cell of every row painted with a
configured background, no terminal-default gaps, deletion/insertion bars
spanning the full column width.

**Status: DONE.** All findings (TH-1..TH-6) fixed and all tests (TH-T0..TH-T5)
landed, including the optional full-line-highlight (TH-2/TH-5) and single-knob
(TH-4) features. Suite **39 cases / 1397 assertions, green** (debug + ASAN);
integration patch round-trip green. Shipped an example
`extras/theme_inverted.conf` and smoke-tested it on the binary (`diffy -s`).
Red→green proven: reverting the renderer fixes flips the theme tests to failing
(the oracle self-check stays green). New test files: `output/theme_tests.cc` +
shared `output/render_test_util.hpp`; corpus no-gap sweep folded into
`output/render_corpus_tests.cc`.

**Behavior-neutrality (scoped):** the **default** theme is verified
byte-identical pre/post across both renderers over the corpus. It is *not*
neutral for every conceivable custom theme: TH-2 deliberately makes the
unchanged context of a changed line take the line's tint
(`delete_line`/`insert_line`) instead of `common_line`, and Meta/header padding
take `empty_cell` instead of `common_line`. A theme that set `common_line` (or a
distinct `empty_cell`) but left `delete_line`/`insert_line` empty would render
differently — this is the intended whole-line-tint behavior, and the
`style.background` knob backstops any unset cell.

**Post-review follow-ups applied** (workflow code review, high effort): hoisted
the per-segment token-style concat out of `render_display_line`'s loop; added a
truecolor (`48;2`) theme variant so the production hex-color path is tested (was
palette-only); folded the TH-T4 corpus sweep into the alignment pass so the diff
pipeline runs once per fixture; de-duplicated the ANSI oracle + theme factories
into `render_test_util.hpp`. Deferred (noted, low value): precompiling the
`style.background` escape at config-load, and the by-value `DisplayCommand`
copy — both pre-existing-architecture efficiency nits, not correctness issues.

**Original problem (for context):** foreground recoloring worked; background-
filled themes did **not** render cleanly. Three structural reasons, all in
`src/output/column_view.cc` + `src/config/config.hpp`.

## Model (why this is non-trivial)

Rendering is **per-cell, fully reset**: each styled cell is emitted as
`style + text + "\033[0m"` (`column_view.cc:585-594`). `\033[0m` clears fg, bg,
*and* attributes, so there is **no persistent "page background"** — every cell
must re-assert its own `bg` or the terminal default shows through. The default
themes are all fg-only (`bg = kNone` everywhere, `config.hpp:44-114`), so this
has never been exercised. An inverted theme stresses exactly the gaps below.

## Findings (ranked)

- [x] **TH-1 · Trailing padding ignores line type** — FIXED (`column_view.cc`).
  Padding now uses a new `line_style(config, type)` helper keyed on the row's
  edit type, so a deletion/insertion fills the full column width. Was
  `config.style.common_line` regardless of type → partial-width bars. Regression:
  `theme_tests.cc` "deletion fills the full column width" (common_line bg must
  not leak into a deleted row).

- [x] **TH-2 · Whole-line background is never applied** — FIXED
  (`column_view.cc`). `render_display_line` now paints each segment with the
  line's base style (`line_style`) and layers the token style on top, so the tint
  of a delete/insert line covers its unchanged context too — not just the changed
  tokens. Regression: `theme_tests.cc` "changed line is tinted whole" (common_line
  bg appears nowhere on a fully-changed row; both delete/insert line bgs present).

- [x] **TH-3 · Uncolored gaps punch holes** — FIXED (`column_view.cc`). The
  line-number cell now falls back to the row's base style (not `""`) when
  `context_colored_line_numbers=false`, so the number always carries a background.
  (The `empty_cell` separators were already styled; an inverted theme sets that
  key, and TH-4's global background backstops anything missed.) Regression:
  `theme_tests.cc` "no default-bg gaps" runs the uncolored-line-number path.

- [x] **TH-4 · No single background knob** — DONE (renderer + config). Added
  `style.background` (`config.hpp`, wired through `config.cc` options +
  escape-code compile). The renderer prepends it under every cell, so one key
  fills the whole view; per-cell styles layer on top. Empty by default →
  byte-identical to fg-only themes. Regression: `theme_tests.cc`
  "style.background alone fills every cell". Example `extras/theme_inverted.conf`
  ships and was smoke-tested on the binary.

- [x] **TH-5 · Token cells full-reset the background** — FIXED (with TH-2). Token
  cells now emit `line_base + token_style + text + "\033[0m"`; because the token
  styles in a real inverted theme set only a foreground, the line background shows
  through. Verified end-to-end: the example theme renders changed tokens as
  `<line-bg> + <token-fg>` (highlight layered over the tint). Covered by the
  token-foreground assertions in "changed line is tinted whole".

- [x] **TH-6 · Cosmetic typo** — FIXED. Dropped the stray double unary plus in
  `column_view.cc` (`right[i] + + "\033[0m"` → `right[i] + "\033[0m"`); also
  removed 2 pre-existing clang-format deviations as a side effect.

## Tests — prove the inverted scheme actually works

Test-first, per the guiding principles: the assertions below describe the
*correct* inverted output and therefore **fail before TH-1..3, pass after**.
All drive the pure `column_view_render_lines(diff_input, hunks, config, options,
width)` seam (P1a) with a hand-built `ColumnViewState` — **no disk, no
`config.cc`** — exactly like `output/column_view_tests.cc` does today. The
difference: here `config` sets backgrounds, so the rows contain ANSI and the
tests assert *on* the escape codes.

- [x] **TH-T0 · Test oracle `bg_runs(row)` + `inverted_theme()`** — DONE
  (`output/theme_tests.cc`). `bg_runs` parses a styled row into
  `vector<{bg_id, visible_codepoints}>`, tracking the active SGR background
  (handles `0`/`49` reset, 16-color `40-47`/`100-107`, `48;5;n`, `48;2;r;g;b`, and
  skips `38;…` foregrounds). Helpers `bg_set`/`has_default_bg` on top. A self-check
  test pins the oracle (fg-change ≠ bg; bg-under-fg stays active). `inverted_theme()`
  sets a **distinct 256-color** bg on every key; token keys set fg-only (proves
  layering). A separate `bg_only_theme` path covers TH-T5.

- [x] **TH-T1 · Full-width deletion bar** — DONE. Deletes a middle line; asserts
  the deleted row contains the `delete_line` bg and **no** `common_line`/
  `common_line_number` bg (the padding no longer leaks common). Red before TH-1.

- [x] **TH-T2 · No default-background gaps, whole row** — DONE. Mixed
  insert/delete/common/length-mismatch diff; asserts `has_default_bg` is false for
  **every** row, run once with `context_colored_line_numbers=true` and once
  `false` (the TH-3 path). The headline "fully inverted works" guarantee.

- [x] **TH-T3 · Whole-line tint + token layering** — DONE. Partially-changed line;
  asserts both the delete-line and insert-line bgs are present, `common_line` bg
  is absent (so unchanged segments took the line tint, TH-2), and the delete/insert
  **token foregrounds** are present (highlight layered over the tint, TH-5). Red
  before TH-2.

- [x] **TH-T4 · Corpus no-gap sweep** — DONE (`output/render_corpus_tests.cc`).
  Re-runs the column-view pass over all **65** fixtures under `inverted_theme()`
  and asserts no visible cell (padding spaces included — where TH-1 hid) is left on
  the terminal-default background. Exercises tabs, multi-byte glyphs, wrapped
  780-char lines.

- [x] **TH-T5 · Single-knob inheritance** — DONE. Sets *only* `style.background`
  and asserts every row is gap-free and every run is that one background — proving
  the base color propagates to every cell (TH-4).

## Execution sequence — COMPLETED

Followed the test-first rule (red → green); all steps landed:

1. [x] **TH-T0** oracle + `inverted_theme()` factory.
2. [x] **TH-T1, TH-T2** → **TH-1** + **TH-3** (+ **TH-6** typo, same function).
3. [x] **TH-T4** corpus sweep — confirms 1–2 across all 65 fixtures.
4. [x] **TH-T3** → **TH-2** + **TH-5** (full-line tint + token layering).
5. [x] **TH-T5** → **TH-4** (single `style.background` knob); shipped
   `extras/theme_inverted.conf`, smoke-tested on the binary + `make
   sanitize-address` green.

**Verification recap:** 40 cases / 1388 assertions green (debug + ASAN);
integration patch round-trip green; default-theme output byte-identical pre/post
over the corpus; reverting the renderer fixes flips 5/6 theme tests to red while
the oracle self-check stays green (proving the tests bite). New formatting
deviations introduced: **zero** (clang-format 17, checked against the HEAD
baseline).

## Bundled themes (follow-up)

First-run setup now seeds four popular, **fully self-contained** example themes
next to `theme_default` — `theme_dracula`, `theme_nord`, `theme_solarized_dark`,
and `theme_github_light` (the light one doubles as the "looks the same on any
terminal" case). Each sets a foreground *and* background on every element plus a
base `style.background`, so it renders identically regardless of the terminal's
default colors — exercising exactly the inverted-theme rendering above.

- Stored as raw `.conf` text in `config.cc` (hex colors can't round-trip through
  `TermStyle::to_value()`, which only emits palette names; the load path parses
  hex into `48;2` truecolor fine). Exposed via `config_bundled_themes()`.
- `config_write_bundled_themes()` writes each on first run, **skipping any that
  already exist** so user edits are never clobbered; listed in the generated
  `diffy.conf` comment for discoverability.
- Tested: each bundled theme parses (`cfg_parse_value_tree`) and renders a mixed
  diff with **no terminal-default gaps** (`theme_tests.cc`). Smoke-tested
  end-to-end on the binary with a sandboxed `HOME` — first run creates all four,
  and `theme_dracula` renders with the expected truecolor backgrounds.
