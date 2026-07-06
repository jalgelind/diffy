# Binary (hex) diffing — design

Status: proposal. Pure C++, **no new third-party dependencies** (Rust or otherwise).
We borrow the *algorithms* from prior art (biodiff, rsync/FastCDC, Needleman–Wunsch,
Myers) and implement them ourselves in `libdiffy`.

## Goal

Diff two binary files and render the result in both of diffy's existing view modes:

- **Unified hex** — one offset column, interleaved removed/added byte rows, ASCII gutter.
- **Side-by-side hex** — two aligned hex panes sharing logical offsets, gaps padded.

The output must reuse the existing theme/palette, colour-decision, terminal-width and
tty machinery, so a binary diff looks like the rest of diffy.

## The core problem: alignment, not comparison

Positional byte-compare tools (`vbindiff`, `dhex`, `cmp -l`, `diff <(xxd a) <(xxd b)`)
all break on insertions/deletions: a single inserted byte shifts every following byte,
so everything after the edit is reported as "changed."

The fix, used by biodiff, is **alignment with gaps**: matched bytes stay column-aligned
and an insertion/deletion becomes a visible gap on one side rather than a cascade.
biodiff does this with gap-affine pairwise sequence alignment (Needleman–Wunsch /
Wavefront). That's the right *model*; the question is only how to make it scale without
pulling in an alignment library.

Naive global DP alignment is O(n·m) time and space — biodiff itself caps global mode at
~64 kB. We can't run that over a multi-MB binary.

## Approach: two-tier alignment, reusing the existing diff engine

### Tier 1 — coarse alignment over content-defined chunks (reuses Myers/patience)

Key observation: `Algorithm<Unit>` (`libdiffy/algorithms/algorithm.hpp`) is already
generic over any `Unit` that has `hash()`, `operator==`, and `operator<`. `Line` is just
one such unit, matched purely by checksum. We can define a **`Chunk`** unit and get
Myers greedy / Myers linear / patience alignment over chunks *for free*.

- Split each file into variable-length chunks using a **content-defined chunking (CDC)**
  boundary rule — a Gear/Rabin rolling hash over a sliding window, cutting when the low
  bits of the hash hit a target mask (FastCDC-style), with min/max chunk-size clamps.
  Because boundaries are content-relative, a local edit perturbs only one or two chunks;
  the rest of the file re-synchronises. This is what stops the cascade at coarse grain.
- Each chunk becomes `Chunk{ offset, length, hash }`. Feed `span<Chunk>` into the
  existing `Algorithm<Chunk>` → an edit sequence of `Common` / `Delete` / `Insert`
  chunks. `compose_hunks` can group them exactly as it does for lines.

This tier is cheap: O(#chunks) input to an already-tuned diff, and chunk hashes are
unique enough to keep Myers's `D` small.

**Hashing and collisions.** `Chunk::hash()` returns the in-tree 32-bit `crc32c`
(matching `Line`, and required because patience keys an `unordered_map<uint32_t,…>`
on it). `operator==` compares checksum **and** length; `operator<` orders by checksum
then length (patience sorts, so ordering must be total and deterministic). A 32-bit
checksum can in principle collide, so the collision guard — not the hash width — is what
guarantees correctness: when the chunk aligner reports a `Common` chunk, `memcmp` the two
byte ranges before trusting it, and demote to a removed+added region on mismatch. That
makes false "equal" impossible regardless of hash width, so a wider hash buys nothing.

### Tier 2 — fine byte alignment inside changed regions (optional, bounded)

Chunk-level output marks *regions* as replaced/inserted/deleted but not individual
bytes. For per-byte colouring, run a byte-level alignment **only within a replaced
region** (an adjacent Delete-run + Insert-run):

- Implement **banded Needleman–Wunsch** (Hirschberg linear-space, or a fixed diagonal
  band) in `libdiffy/algorithms/`. Restricting to a band bounds cost to O(n·band).
- Cap the region size (e.g. a few hundred kB). Above the cap, skip the fine pass and
  render the region as whole-run delete+insert — correct, just coarser.

Tier 2 is what upgrades "these 3 chunks changed" into "byte 0x40 is 0x9A vs 0x9B, and
4 bytes were inserted at 0x41."

### Small-file fast path

Below a threshold (say 256 kB total), skip CDC and run byte-level banded NW directly
over the whole file for the tightest possible alignment — mirrors biodiff's opt-in
global mode. Exposed as `--hex-global`.

## Data model — one alignment, two renderers

Produce a single backend-agnostic alignment structure and render it into both views,
the same way `unified.cc` and `column_view.cc` share hunks today.

```
struct HexSegment {
    enum class Kind { Equal, Replace, InsertA, InsertB } kind; // A = left, B = right
    uint64_t a_offset, a_len;   // bytes on the left  side (0 len for InsertB)
    uint64_t b_offset, b_len;   // bytes on the right side (0 len for InsertA)
};
using HexAlignment = std::vector<HexSegment>;
```

**Building the alignment (the coalescing step, `hex_align`).** The chunk aligner emits
per-chunk `Common`/`Delete`/`Insert` edits. Coalesce them into `HexSegment`s:
consecutive `Common` chunks → one `Equal` segment; a run of `Delete` chunks immediately
followed by a run of `Insert` chunks → one `Replace` segment (then optionally refined by
tier 2 into finer Equal/Replace/Insert sub-segments); a lone `Delete` run → `InsertA`
(A-only), a lone `Insert` run → `InsertB` (B-only). Byte offsets/lengths come from the
chunk records. Segments are contiguous and cover both files end to end — an invariant
worth asserting in tests.

- **Side-by-side renderer** walks segments, emitting N-byte (default 16, `--bytes-per-row`)
  rows per pane, padding the shorter side across a gap so offsets stay aligned. Per-byte
  style: equal = dim, replaced = changed colour, inserted/deleted = add/remove colour.
  Each pane shows offset + hex + ASCII columns; gap padding renders as blanks.
- **Unified renderer** walks the same segments, emitting `-` rows for A-only bytes,
  `+` rows for B-only bytes, and context rows for equal runs, each with offset + hex +
  ASCII columns. Offset-column width is sized to the larger file.

Colours come straight from `ColumnViewTextStyleEscapeCodes` (the existing theme
palette), so themes and `--color` Just Work.

## Wiring into the CLI

Input:
- Add `read_file_bytes(path) -> std::vector<uint8_t>` in `libdiffy/util/` (raw read;
  mmap is a later optimisation). Binary path does **not** go through `readlines`.
- Promote `looks_binary()` (currently file-static in
  `highlight/syntax_highlighter.cc`, NUL-scan of first 1024 bytes) to a shared
  `util/` helper and reuse it for detection.

Dispatch (`cli/diffy_main.cc`, around the current `readlines` at line 500):
- Decide binary vs text: `--binary`/`--text` override; otherwise auto = either side
  `looks_binary()`. If binary → read bytes, align, render hex; else the existing path.
- Reuse `-u` / `-s` to choose unified vs side-by-side within hex mode.

Flags (`parse_args`, `long_options`, `ProgramOptions` in `config/config.hpp`):
- `--binary` / `--text` (or `--binary=auto|always|never`) — new `kOpt*` constants.
- `--bytes-per-row N` (default 16).
- `--hex-global` — force whole-file byte alignment (small files / opt-in).
- New `ProgramOptions` fields: `binary_mode`, `bytes_per_row`, `hex_global`.

Minimum git-compatible fallback: when a file is binary and no hex renderer applies
(e.g. above the size cap with alignment disabled), print `Binary files A and B differ`
and exit 1 — behaviour diffy lacks today.

## Module layout

```
libdiffy/util/read_bytes.{hpp,cc}        # raw byte reader (fallback path)
libdiffy/util/mapped_file.{hpp,cc}       # FileBytes: mmap large files, read small
libdiffy/util/binary_detect.{hpp,cc}     # promoted looks_binary()
libdiffy/binary/chunker.{hpp,cc}         # Gear/Rabin CDC -> vector<Chunk>
libdiffy/binary/hex_align.{hpp,cc}       # tiers 1+2 -> HexAlignment
libdiffy/algorithms/needleman_wunsch.hpp # banded/Hirschberg byte aligner (tier 2)
libdiffy/output/hex_unified.{hpp,cc}     # CLI: unified hex -> ANSI lines
libdiffy/output/hex_column.{hpp,cc}      # CLI: side-by-side hex -> ANSI lines
libdiffy/render/hex_view_model.{hpp,cc}  # build_hex_view: HexAlignment -> DiffViewModel (GUI/embedders)
```

## Performance / large files

- Tier 1 is linear in chunk count; CDC read is a single streaming pass.
- Tier 2 is bounded per replaced region and skippable above a cap.
- Size threshold (configurable) selects: small → global byte NW; medium → CDC + bounded
  fine pass; huge → CDC coarse only, or `Binary files differ`.
- `log`/report what was truncated rather than silently degrading, matching diffy's
  existing honesty about skipped work.

## Testing

- **Unit:** chunker boundary determinism (same bytes → same cuts); alignment on a file
  with a single early insertion must stay aligned afterward (the anti-cascade
  invariant); tier-2 banded NW on small crafted regions.
- **Golden** (`tests/testsuite.py`): add a small binary fixture under
  `tests/golden/inputs/` (e.g. a base blob + a copy with a byte flip and a short
  insertion) and golden `.out` snapshots for hex-unified and hex-sidebyside, colour and
  plain. Deterministic already via `SOURCE_DATE_EPOCH` + fixed `-W`.

## Phasing

1. **Detection + fallback.** `looks_binary` promotion, `read_file_bytes`,
   `--binary`/`--text`, `Binary files differ`. Smallest useful, git-parity.
2. **Coarse hex unified.** CDC chunker → `Algorithm<Chunk>` → `HexAlignment` →
   `hex_unified` renderer. Proves the one-alignment-two-renderers model.
3. **Hex side-by-side.** `hex_column` renderer over the same `HexAlignment`.
4. **Fine byte alignment.** Banded NW tier-2 for per-byte colouring; `--hex-global`.
5. **Scaling.** mmap, size thresholds, truncation reporting, `--bytes-per-row`.

## As built (deviations from the proposal above)

- **Common prefix/suffix trimming was the key robustness fix.** Content-defined
  chunking re-synchronises a byte or two *late* after a length change (the boundary
  near `min_size` shifts), so a 4-byte insertion in a 256 KB file first surfaced as
  ~12 KB of churn that overflowed the byte-refine cap. Trimming the common prefix and
  suffix of every changed region before refining collapses a shifted insertion/deletion
  to just its differing core — a 4-byte insert now renders as a 4-byte insert. This runs
  in `hex_align` before the byte aligner and is what makes the anti-cascade actually hold.
- **Chunk hash is 32-bit crc32c + memcmp guard**, not a widened 64-bit hash (see above).
- **No separate "Binary files differ" mode.** The hex renderers always run; the only
  graceful degradation is the `truncated` note when a changed region is too large to
  byte-refine (shown as whole removed/added blocks).
- **Side-by-side auto bytes-per-row** picks the largest multiple of 8 that fits the
  terminal (falling back to the largest count that fits when even 8 won't); `--bytes-per-row`
  overrides. Unified defaults to 16.
- **Structured render path for embedders.** `render/hex_view_model.hpp` (`build_hex_view`)
  turns a `HexAlignment` into a `DiffViewModel` — the same model the text diff produces —
  so a frontend that already renders `DiffViewModel` (the GUI) shows hex diffs with no new
  rendering code: equal bytes are `SpanStyle::Common`, removed/added bytes are Delete/Insert
  tokens, offsets ride as leading spans. The CLI keeps its own `hex_unified`/`hex_column`
  ANSI renderers (golden-locked); the two paths share `hex_align` + `hex_common`. Unifying
  the CLI onto the view model is a possible later cleanup.
- **Refinement:** the fine-pass refine gate is a DP-cell budget (`byte_cap²`) with a
  per-side ceiling, not a flat per-side cap — so a tall/thin changed region (a big deletion
  against a few added bytes) still byte-refines while table memory stays bounded (~byte_cap²).
- **Performance:** the byte aligner is now a **banded** DP (diagonal half-width doubles when
  the optimal path touches the band edge; full-DP fallback), with the cost cell narrowed to
  uint8/uint16/uint32 by the max distance. Substitution-heavy or structurally-aligned regions
  finish in a tiny band — measured ~10–14× vs the old full O(n·m) on 4 KB regions. Rendering
  uses hex/offset **lookup tables** (no per-byte `fmt::format`). The CLI also **summarises**
  rather than dumping a hex diff when a large amount of content can't be meaningfully aligned
  (coarse + >512 KiB, or >8 MiB changed). See `docs/binary-diff-roadmap.md` for what's left.
- **mmap is implemented** via `util/mapped_file.hpp` (`FileBytes`): regular files ≥ 64 KiB
  are memory-mapped (POSIX); small files, non-regular inputs (FIFOs, `/dev/null` from git
  difftool), Windows, and any mmap failure fall back to a plain read. The hex pipeline
  takes `gsl::span<const uint8_t>` so it runs over either backing with no copy.
- **Done since:** banded byte aligner + narrow DP cells (above). Still deferred: Hirschberg
  linear-space traceback (banding already bounds memory to O(n·band) in the common case).

## Prior art referenced (algorithms only, nothing linked/vendored)

- **biodiff** — gap-aligned hex view; validates the alignment-with-gaps model and the
  equal/changed/gap colour scheme.
- **rsync / FastCDC** — rolling-hash content-defined chunking for insertion-robust
  coarse alignment.
- **Needleman–Wunsch / Hirschberg / Myers** — pairwise alignment; NW (banded/linear
  space) for the fine pass, Myers already in-tree for the chunk pass.
- **radiff2** — reference for hex unified vs two-column output formats.
