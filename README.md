Diffy
=====

A diff toolkit with two frontends over one diff engine:

- **`diffy`** — a side-by-side / unified diff tool for the terminal.
- **`diffy-gui`** — a desktop **git client** (built with [Slint](https://slint.dev))
  that opens repositories, browses changes and commits, and renders diffs with
  crisp text in unified and side-by-side modes.

Written for fun after reading the diff chapters in `Building Git` [1].


Architecture
------------

```
libdiffy/        backend-agnostic diff engine (no terminal, no GUI, no git)
  algorithms/      myers greedy/linear, patience
  processing/      tokenizer, hunk composition + annotation
  render/          DiffViewModel + build_diff_view (styled spans, both layouts)
                   diff_pipeline (text in -> annotated hunks / view model out)
  highlight/       tree-sitter syntax highlighting (groups, palette, grammars)
  config/          diffy.conf parsing, themes, [gui] settings, repos.conf
  util/            readlines, utf8, hashing, colour values
cli/             the `diffy` terminal app (getopt, tty, ANSI output)
gui/             the `diffy-gui` git client (Slint UI + libgit2 repo model)
tests/           doctest unit + corpus tests
```

The core produces a backend-agnostic *render model* (rows of semantically-styled
spans). The CLI serialises it to ANSI; the GUI serialises it to a Slint model.
Neither frontend re-implements diffing. libgit2 only supplies repository and blob
data to the GUI — the diffing is always libdiffy's.


Build (CMake — primary)
-----------------------

Dependencies: a C++20 compiler, `cmake` (>= 3.21), `ninja`.
The GUI additionally needs a Rust toolchain (`cargo`, to build Slint from source
the first time) and `libgit2` (`brew install libgit2`).

```
# CLI + tests (default)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ctest --test-dir build

# add the GUI git client
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDIFFY_BUILD_GUI=ON
ninja -C build diffy-gui
```

Options: `DIFFY_BUILD_CLI` (ON), `DIFFY_BUILD_GUI` (OFF), `DIFFY_BUILD_TESTS` (ON),
`DIFFY_ENABLE_HIGHLIGHT` (ON).


Syntax highlighting
-------------------

Diffs are syntax-highlighted with [tree-sitter](https://tree-sitter.github.io/).
Highlighting is a second dimension layered on top of the diff: the engine parses
each whole buffer once (in `libdiffy/highlight/`), maps tree-sitter captures to a
small set of `HighlightGroup`s, and splits the diff spans at syntax boundaries.
The GUI colours the span foreground while keeping the add/remove backgrounds; the
CLI emits truecolor foreground escapes (which carry no display width, so column
alignment is unaffected).

The language is detected from the file's extension (and a few filenames like
`CMakeLists.txt`). Supported today: C, C++, Go, Rust, Java, C#, Python, Ruby,
Bash, JavaScript, TypeScript, TSX, HTML, CSS, Lua, TOML, CMake, Markdown, JSON.

- **Toggle:** GUI — the "Syntax" checkbox in the option bar (persisted in
  `[gui] syntax_highlight`); CLI — `--no-highlight`.
- **Theme the colours:** add a `[gui.syntax]` table to `diffy.conf` mapping
  group names to hex, e.g. `keyword = "#c586c0"`, `string = "#ce9178"`,
  `comment = "#6a9955"`, `type = "#4ec9b0"`, `function = "#dcdcaa"`,
  `number = "#b5cea8"`, `variable = "#9cdcfe"`. Unset groups use the built-in
  default palette.
- **Add a language:** add one `add_ts_grammar(<lang> <repo> <tag> [SUBDIR ..])`
  line in `libdiffy/highlight/treesitter.cmake` and a registry entry
  (`language.cc`: extension, `tree_sitter_<lang>` binding, query chain). Grammars
  are fetched with `FetchContent` (pinned) and their `highlights.scm` is baked
  into the binary. The bundled tree-sitter runtime is 0.25 (language ABI 13–15),
  so most current grammar releases work. For a grammar that ships no query, pass
  `LOCAL_QUERY <path>` to point at one shipped under `highlight/queries/`.
- Disable the whole feature (and the grammar fetches) with
  `-DDIFFY_ENABLE_HIGHLIGHT=OFF`. Buffers above a size cap, binary content, and
  unknown languages render unhighlighted.

Note: with `build-windows.cmd`, adding grammars needs an explicit `reconfigure`
(the incremental build doesn't re-run CMake on a `treesitter.cmake` edit).


Build (Windows)
---------------

`build-windows.cmd` wraps the CMake build with MSVC (Visual Studio 2022) or
MinGW (MSYS2 UCRT64), initializing submodules automatically.

    > build-windows.cmd release            REM CLI (Release)
    > build-windows.cmd release gui run     REM build + run the GUI
    > build-windows.cmd release all         REM CLI + GUI + tests
    > build-windows.cmd test                REM Debug build + ctest

The GUI additionally needs a Rust toolchain (cargo, for Slint) and libgit2 —
easiest via vcpkg (`vcpkg install libgit2 pkgconf`, then set `VCPKG_ROOT`).
Run `build-windows.cmd help` for all options.


Build (Make — Unix/WSL convenience)
-----------------------------------

A thin `Makefile` wraps the CMake build above so a plain `make` works in a
POSIX shell. Build trees go under `$(OUT)/<platform>-<config>` (platform from
`uname`; `OUT` defaults to `out/`), so they never clash with the Windows/MSVC
tree or each other. The GUI is gated behind `gui*` targets (it also needs cargo
+ libgit2).

    $ make            # debug CLI + tests  -> out/linux-debug/cli/diffy
    $ make release    # release            -> out/linux-release/cli/diffy
    $ make test       # build + ctest
    $ make gui        # build diffy-gui (needs cargo + libgit2)
    $ make clean      # remove the make build trees (leaves out/release/build-msvc)

(On macOS the prefix is `macos-` instead of `linux-`.)


Configuration
-------------

Both apps read `diffy.conf` from your config directory (`diffy --help` prints the
path). Shared sections (`general`, `settings`, `chars`, `style`, `color_map`) are
honoured by both. The GUI adds:

- a `[gui]` table in `diffy.conf` (view mode, font, theme variant, window size,
  whether to restore the last repo);
- `repos.conf`, the list of repositories the GUI has opened (most-recent first,
  with pinning).


Testing
-------

    $ ctest --test-dir build               # unit + corpus (doctest)
    $ tests/testsuite.py out/release/diffy  # integration: diff -> patch round-trip

Test cases are constructed from the git history of files from various projects;
create new ones with `./extras/git-extract-test-cases`.


Using diffy with git (CLI)
--------------------------
Copy `extras/diffy-git` somewhere on your `$PATH`; it invokes `diffy` as a
difftool and allows negative numbers similar to `git show -2`.

    $ git diffy -2


References
----------
    [1] https://blog.jcoglan.com/2017/02/12/the-myers-diff-algorithm-part-1/
