Diffy
=====

A diff engine and terminal diff tool:

- **`libdiffy`** — a backend-agnostic diff + render engine (no terminal, no GUI,
  no git). It turns two text buffers into a styled *render model*.
- **`diffy`** — a side-by-side / unified diff tool for the terminal, built on
  `libdiffy`.

A desktop **git client** frontend, **`diffy-gui`** (Slint + libgit2), lives in a
separate repository and consumes `libdiffy` as a library — so this repo stays
free of any GUI/git dependencies.

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
  config/          diffy.conf parsing, themes, color_map (shared with frontends)
  util/            readlines, utf8, hashing, colour values
cli/             the `diffy` terminal app (getopt, tty, ANSI output)
tests/           doctest unit + corpus tests
```

The core produces a backend-agnostic *render model* (rows of semantically-styled
spans). The CLI serialises it to ANSI; a GUI frontend serialises the same model
to its own widgets. Neither frontend re-implements diffing — the diffing is
always libdiffy's.


Build (CMake — primary)
-----------------------

Dependencies: a C++20 compiler, `cmake` (>= 3.21), `ninja`.

```
# CLI + tests (default)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ctest --test-dir build
```

Options: `DIFFY_BUILD_CLI` (ON), `DIFFY_BUILD_TESTS` (ON),
`DIFFY_ENABLE_HIGHLIGHT` (ON).


Syntax highlighting
-------------------

Diffs are syntax-highlighted with [tree-sitter](https://tree-sitter.github.io/).
Highlighting is a second dimension layered on top of the diff: the engine parses
each whole buffer once (in `libdiffy/highlight/`), maps tree-sitter captures to a
small set of `HighlightGroup`s, and splits the diff spans at syntax boundaries.
The CLI emits truecolor foreground escapes (which carry no display width, so
column alignment is unaffected).

The language is detected from the file's extension (and a few filenames like
`CMakeLists.txt`). Supported today: C, C++, Go, Rust, Java, C#, Python, Ruby,
Bash, JavaScript, TypeScript, TSX, HTML, CSS, Lua, TOML, CMake, Markdown, JSON.

- **Toggle:** `--no-highlight`.
- **Colours:** the CLI uses the built-in default palette. (Per-group colour
  overrides are a frontend feature — the GUI reads a `[gui.syntax]` table from
  `diffy.conf`; the CLI does not.)
- **Map an extension:** add a `[highlight.extensions]` table to `diffy.conf`
  mapping a language to an extension (or list), e.g. `cpp = ".ino"` — the diff
  engine reads it to override language detection.
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
    > build-windows.cmd test                REM Debug build + ctest

Run `build-windows.cmd help` for all options.


Build (Make — Unix/WSL convenience)
-----------------------------------

A thin `Makefile` wraps the CMake build above so a plain `make` works in a
POSIX shell. Build trees go under `$(OUT)/<platform>-<config>` (platform from
`uname`; `OUT` defaults to `out/`), so they never clash with the Windows/MSVC
tree or each other.

    $ make            # debug CLI + tests  -> out/linux-debug/cli/diffy
    $ make release    # release            -> out/linux-release/cli/diffy
    $ make test       # build + ctest
    $ make clean      # remove the make build trees (leaves out/release/build-msvc)

(On macOS the prefix is `macos-` instead of `linux-`.)


Configuration
-------------

`diffy` reads `diffy.conf` from your config directory (`diffy --help` prints the
path). Sections `general`, `settings`, `chars`, `style`, `color_map` control the
diff output; a GUI frontend reads the same file and adds its own `[gui]` table
and a `repos.conf` (neither is used by the CLI).


Testing
-------

    $ ctest --test-dir build               # unit + corpus (doctest)
    $ tests/testsuite.py out/release/diffy  # integration: diff -> patch round-trip

Test cases are constructed from the git history of files from various projects;
create new ones with `./extras/git-extract-test-cases`.


Using diffy with git (CLI)
--------------------------
Copy `extras/git-diffy` somewhere on your `$PATH`; it invokes `diffy` as a
difftool and allows negative numbers similar to `git show -2`.

    $ git diffy -2


References
----------
    [1] https://blog.jcoglan.com/2017/02/12/the-myers-diff-algorithm-part-1/
