# Binary (hex) diff — roadmap

Companion to `docs/binary-diff.md` (the design). This is the forward-looking
plan: what shipped, what's known-weak, and the order to improve it.

Sizes: **S** ≈ <1h · **M** ≈ a few hours · **L** ≈ a day+.
Impact is relative to a real user diffing a binary.

## Shipped

- Alignment core: content-defined chunking (Gear rolling hash) → existing
  patience diff over chunks → common prefix/suffix trim → bounded byte-level
  Needleman–Wunsch refine. Small files (or `--hex-global`) byte-align whole.
- CLI: `--binary`/`--text`, `--bytes-per-row`, `--hex-global`; unified
  (`hex_unified`) and side-by-side (`hex_column`) ANSI renderers; side-by-side
  auto-picks the largest multiple-of-8 bytes/row that fits.
- `FileBytes`: mmap for regular files ≥ 64 KiB, read fallback otherwise.
- Facade: `build_hex_view()` → `DiffViewModel`, so any structured frontend
  renders hex with no new code.
- GUI (diffy-gui): binary files render as a hex diff at the working-tree,
  commit, ref, and PR-single-commit single-file seams.
- Tests: 11 unit + 3 golden (CLI) + GUI builds & links.

## Known limitations (from review)

- **Performance** — the O(n·m) byte DP dominates (measured ~245 ms for a
  4 KB×4 KB region / any refine up to `byte_cap²`); rendering uses
  `fmt::format` per byte (~2.1 s for a 2 MB×2 MB dissimilar diff). See
  `docs/binary-diff.md` and the perf items below.
- **GUI blocks the UI thread** — `open_file → relayout → hex_align +
  build_hex_view` runs synchronously; the text path backgrounds large diffs
  (`kBackgroundDiffBytes`) but hex doesn't. A ≤2 MB binary (the classify cap)
  can freeze the window ~2 s.
- **GUI coverage gaps** — the all-files view and the PR-aggregate/network path
  (`render_pr_blobs`) still show the "Binary file" note, not hex. Worse,
  `render_pr_blobs` doesn't classify blobs at all, so a binary file opened in a
  network-sourced PR is currently text-diffed into garbage (latent, predates hex).
- **Three hex renderers** (`hex_unified` ANSI, `hex_column` ANSI,
  `hex_view_model` spans) each re-derive the segment-walk + context-trim logic;
  unified trims per-row, side-by-side per-byte — they can drift.
- **Golden coverage is global-path only** — the chunk + refine path is
  unit-tested but not locked by a CLI golden.
- **Cross-repo divergence** — two diffy branches carry the feature:
  `feature/binary-hex-diff` (master-based) and `feature/binary-hex-diff-gui`
  (= the GUI's pinned `force_language` commit + hex). The GUI submodule pins the
  latter.

## Work items

### Performance
- **P1 — Band the byte aligner. ✅ DONE.** Diagonal band, doubling when the
  optimal path touches the edge, full-DP fallback. Measured: 4 KB×4 KB
  dissimilar 245→18 ms, 4 KB refine region 250→24 ms.
- **P2 — Narrow DP cell type. ✅ DONE.** uint8/uint16/uint32 by max distance.
- **P3 — Hex/offset lookup tables. ✅ DONE.** No per-byte `fmt::format`; render
  cost of the 2 MB dissimilar cases dropped ~4× on its own.
- **P4 — Cap pathological output. ✅ DONE.** CLI summarises ("Binary files …
  differ (N bytes …)") when coarse + >512 KiB or >8 MiB changed. The 2 MB×2 MB
  dissimilar cases went 2140/2300 ms → ~26 ms.
- **P5 — Reuse a grow-only scratch DP buffer (S, low).** Avoid re-allocating up
  to 64 MB per refine region. Smaller once P1 lands.
- **P6 — Drop the third full-data pass (M, low; matters at GB scale).** Widen
  the chunk hash (2×crc32c/xxhash) to skip the `memcmp` collision guard, or fuse
  crc into the Gear scan.
- **P7 — Parallelize independent refine regions (M, low).** Thread pool over
  regions; most diffs have one, so low priority.

### GUI
- **G1 — Background large hex diffs (M, high).** Route hex through the same
  off-thread path as text (or a size threshold) so big binaries don't freeze the
  UI. Pairs with P1/P3 (which shrink the cost in the first place).
- **G2 — Classify PR-network blobs + hex there (M, med).** Make
  `render_pr_blobs` call `classify_blob_pair`; route "Binary file" to hex. Fixes
  the latent garbage-text-diff bug and extends hex to PR-aggregate single files.
- **G3 — Hex in the all-files view (M, low).** Emit hex rows into the
  concatenated model instead of the note stub.
- **G4 — Offset in the gutter (S, low).** Optionally show the hex offset in the
  line-number column instead of inline, for a cleaner look.

### Correctness & testing
- **T1 — CLI golden for the chunk+refine path (S, med).** Add a fixture large
  enough to exercise chunking + a refined region; lock the output.
- **T2 — Non-regular input coverage (S, low).** FIFO / `/dev/null` through
  `FileBytes` fallback and the CLI dispatch.
- **T3 — Collision-guard path (S, low).** Exercise the memcmp-demote branch with
  crafted same-checksum-different-bytes chunks (or document why it's impractical).

### Cleanup / tech-debt
- **C1 — Unify the trimming logic. ✅ DONE (shared helper).** The context-trim
  window math (head/omitted/tail rows + where the "@@" marker lands) is now one
  helper, `hex_equal_window()` in `output/hex_common.hpp`, used by all three
  renderers — output-preserving, and it removes the row-vs-byte drift (unified
  was row-based, side-by-side byte-based). The three renderers still own their
  distinct *layouts* (unified emits separate −/+ rows; side-by-side pairs them;
  the view model emits spans). Fully collapsing the CLI onto `build_hex_view` +
  a `DiffViewModel`→ANSI serializer is possible but has a real tradeoff — the GUI
  wants per-byte foreground colour (`cell.type = Common`) while the CLI wants a
  per-row background + fill-to-edge — so it's left as a larger follow-up.
- **C2 — Expose hex params in config (S, low).** `byte_cap`, chunk sizes,
  default bytes/row, context rows via `diffy.conf`.

### Cross-repo
- **X1 — Reconcile the two diffy branches (S, high).** Forward-port
  `force_language` onto `master` (or merge), rebase the hex work to a single
  branch, and repoint the diffy-gui submodule. Removes the master/release fork.

### Open questions
- **Q1 — "Alphanumeric column."** An ASCII/text column already renders in all
  three views. Confirm whether the ask is (a) GUI visibility (it sits after the
  hex bytes, so a narrow window hides it behind horizontal scroll), (b) genuinely
  missing in some view, or (c) an enhancement (distinct aligned column / toggle).
  Blocks any related work.

## Suggested execution sequence

1. **P3** — trivial, kills the render cliff. *(S)*
2. **P1 + P2** — kill the DP cliff; re-run the perf benchmark to confirm. *(M)*
3. **P4** — bound the worst case now that per-row cost is low. *(S)*
4. **G1** — with P1/P3 landed, background what's left so the GUI never janks. *(M)*
5. **X1** — collapse the branch fork before more GUI work compounds it. *(S)*
6. **G2 → G3** — widen GUI coverage (PR-network, then all-files). *(M)*
7. **C1** — consolidate renderers once behavior is stable (regenerate goldens). *(M)*
8. **T1–T3, P5–P7, C2, G4** — as capacity allows. *(S–M)*

Resolve **Q1** before touching the ASCII column in any of the above.
