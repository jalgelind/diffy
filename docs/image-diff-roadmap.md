# Image diff — investigation & roadmap

For image files (png/jpg/gif/webp/bmp/…), a hex dump is useless. When two
versions of an image differ we want a **visual** diff — in the GUI and, where the
terminal supports it, in the CLI. This is the plan; it builds on the binary-diff
seams already in place (see `docs/binary-diff.md`).

Sizes: **S** ≈ <1h · **M** ≈ a few hours · **L** ≈ a day+.

## Architecture

One shared image-diff core in `libdiffy`, two frontends:

```
libdiffy/image/decode.{hpp,cc}     # bytes -> RGBA8 (+ metadata) via stb_image (+libwebp)
libdiffy/image/image_diff.{hpp,cc} # two RGBA8 -> {similarity%, changed px, overlay RGBA}
                                    #   (pixelmatch algorithm, ported)
cli:  terminal renderer            # protocol detect + encode (sixel/kitty/iterm) or Unicode
gui:  image_bridge + Slint views   # 2-up + overlay + swipe/onion via SharedPixelBuffer
```

The core produces a neutral result (metadata, a similarity score, a changed-pixel
count, and an RGBA overlay bitmap). Each frontend renders that however it can.

## Decoding

- **`stb_image.h`** (single-header, public domain) covers png/jpg/gif/bmp/tga —
  drop into `subprojects/`, zero build cost. Pair with **`stb_image_resize2.h`**
  (also PD) for downscale-to-fit. Add **libwebp** (BSD) only for `.webp`.
- Everything decodes to a common **RGBA8** buffer; the diff + renderers never see
  the format.
- **Untrusted input:** stb has had CVEs and disclaims hostile input. Acceptable
  for local repo files at first; if hardening is needed later, swap PNG/GIF to
  **wuffs** (Apache-2.0, memory-safe by construction) behind the same decode
  interface. Cap decoded dimensions to bound memory.

## Diff engine (shared)

- Port the **pixelmatch** algorithm (ISC, ~few hundred lines): perceptual color
  distance (OKLab + HyAB), a `threshold` knob, and **anti-alias detection** so AA
  fringes aren't flagged. Output: changed-pixel count + similarity %, and an RGBA
  **difference-overlay** (changed pixels magenta over a dimmed original).
- **Dimension mismatch:** pixelmatch needs equal dimensions — if they differ, skip
  the overlay and fall back to 2-up + the metadata diff (optionally diff the
  overlapping crop and mark it).
- **Alpha:** composite over a checkerboard before comparing so transparency isn't
  a false match.
- **Metadata diff** (dimensions, format, color type, bit depth, channels, size)
  needs no full decode (`stbi_info`) and is the guaranteed floor when no
  decoder/protocol is available.

## Terminal output + capability detection

Detection order (each probe skipped when stdout isn't a tty; short timeouts so we
never hang; `--no-image` / `$NO_COLOR` opt out):

1. **not a tty / opted out** → metadata + similarity summary (text only).
2. **kitty graphics** — env hint `$KITTY_WINDOW_ID` / `$TERM=xterm-kitty`, then the
   authoritative query (`ESC _G…a=q…` + DA1 `ESC [c` in one write; graphics reply ⇒ supported).
3. **iTerm2 / WezTerm** — `$TERM_PROGRAM` (`iTerm.app` / `WezTerm`), OSC 1337 `File=`.
4. **sixel** — DA1 `ESC [c`; a `4` in the reply attributes ⇒ sixel.
5. **Unicode fallback** — truecolor half-block `▀` / sextants / octants (à la chafa).
6. **floor** — metadata + similarity summary.

Target pixel size from `CSI 14 t` / `CSI 16 t` (text-area / cell pixel size), else
`TIOCGWINSZ` `ws_xpixel`, else assume ~10×20 px cells; downscale before encoding.

**Pragmatic shortcut:** vendor / shell out to **chafa** (or libchafa) to get all of
sixel/kitty/iTerm2 + Unicode fallback + detection *for free* initially, then
replace with native encoders (sixel is simple; kitty/iTerm2 are base64) if the dep
is unwanted. chafa is LGPLv3+ — check licensing fit before linking.

## GUI (Slint)

- Build the difference bitmap in C++ into a `slint::SharedPixelBuffer<Rgba8Pixel>`
  → `slint::Image::from_rgba8(buffer)`, bound to an `image` property. (The GUI
  already builds `slint::Image`s for avatars, so the plumbing exists.)
- Views: **2-up** (base | modified), a third **difference-overlay** image, plus a
  **swipe** slider and an **onion-skin** alpha slider — both trivial Slint property
  bindings. This is where interactive modes pay off.

## Detection & routing (where it plugs in)

Image files are a subset of "binary". Detect by **magic bytes** (robust) with an
extension hint:

- **CLI** (`cli/diffy_main.cc`): in the binary dispatch, before `hex_align`, sniff
  for an image signature → image path instead of hex. Add `--image`/`--no-image`
  alongside `--binary`/`--text`.
- **GUI** (`gui/src/main.cpp`): `is_binary_path` already lists image extensions and
  `classify_blob_pair` tags binaries. Add an "image" classification (magic-byte
  based) and route it — at the same seam as hex (`open_file` → `relayout`; an
  `image_bridge` analogous to `hex_bridge`) — to the image views instead of the
  hex model. The all-files view keeps the click-to-open stub (same as hex).

## Phasing

- **I0 — Metadata diff (S, high).** Dimensions / format / color type / size, no
  decode. Both CLI + GUI. Immediately better than "binary files differ" for images
  and needs no new deps. Ship first.
- **I1 — Decode + similarity (M).** stb_image → RGBA8; port pixelmatch → similarity
  % + changed-pixel count + overlay bitmap. CLI prints a summary line; core is
  reused by the GUI.
- **I2 — GUI visual diff (M, high).** 2-up + difference overlay via
  `SharedPixelBuffer`. Highest user value, and Slint makes it cheap.
- **I3 — Terminal image output (L).** One protocol via detection (or vendor chafa)
  + Unicode fallback; render overlay, 2-up when dimensions differ.
- **I4 — Polish (M).** GUI swipe + onion-skin sliders; `.webp` via libwebp;
  anti-alias threshold tuning; large-image downscale; native terminal encoders if
  dropping the chafa dep.

## Risks / open questions

- **Dependencies:** stb is free (header); libwebp and (if used) libsixel/libchafa
  are real deps — GUI pulls via vcpkg, CLI via submodule/system. Decide before I3.
- **Untrusted decode:** stb CVEs; consider wuffs for PNG/GIF if it matters.
- **Terminal fragmentation:** detection is fiddly; chafa mitigates but adds LGPL.
- **Animated gif/webp:** diff first frame only at first; note it.

## Suggested order

**I0 → I1 → I2** (metadata, then the shared engine, then the GUI visual diff — all
high value, mostly no terminal-protocol risk), then **I3** (terminal) and **I4**
(polish). Ship the metadata diff (I0) even if nothing else lands.

## Sources

kitty graphics protocol (sw.kovidgoyal.net/kitty/graphics-protocol) · iTerm2 images
(iterm2.com/documentation-images.html) · Sixel (en.wikipedia.org/wiki/Sixel) ·
chafa (github.com/hpjansson/chafa) · pixelmatch (github.com/mapbox/pixelmatch) ·
odiff (github.com/dmtrKovalenko/odiff) · stb (github.com/nothings/stb) · wuffs
(github.com/google/wuffs) · GitHub image diff modes
(github.blog/2014-05-09-rendering-and-diffing-images) · xterm ctlseqs
(invisible-island.net/xterm/ctlseqs) · Slint Image (docs.slint.dev).
