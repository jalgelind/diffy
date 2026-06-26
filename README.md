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

Options: `DIFFY_BUILD_CLI` (ON), `DIFFY_BUILD_GUI` (OFF), `DIFFY_BUILD_TESTS` (ON).


Build (Meson — CLI only)
------------------------

The Meson build is kept for the terminal tool so `make` keeps working; it does
not build the GUI.

    $ make debug      # -> out/debug/diffy
    $ make release
    $ make test


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
