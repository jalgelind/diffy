Diffy
=====

A side-by-side diff tool for the terminal.

Written for fun after reading the diff chapters in `Building Git` [1]

Features
--------

* **Multiple diff algorithms**: Myers (greedy & linear space), Patience, and Streaming Hybrid
* **Side-by-side view**: Column-based display with syntax-aware token diffing
* **Unified diff output**: Compatible with standard patch tools
* **Streaming Hybrid algorithm**: Up to 4.6x faster on large files with identical quality
* **Configurable**: Themes, ignore options, and algorithm selection


Build instructions
------------------

Dependencies: `meson`, `cmake`, `make` and `ninja`. A C++20 compatible compiler.

Build using `meson`.

On POSIX platforms you can use the `make` wrapper to invoke `meson` and `ninja`.

    $ make debug
    $ make release
    $ make test
    $ ls -l out/{debug,release}/diffy

On Windows you can generate a Visual Studio solution.

    $ meson setup --vsenv --backend vs2022 --buildtype=debug out/vs-debug

Testing
-------

Test coverage is very incomplete. There's a few unit tests for testing some components. There's
also an integration test suite that verifies the core algorithms by generating unified diffs and
applying them with `patch`.

    $ tests/testsuite.py out/release/diffy

The test cases are constructed from the git history of various files from different projects.

You can create new ones with


    $ ./extras/git-extract-test-cases ../../path/to/git-repo/and/a/file.xx


The side-by-side view has mostly been tested visually.

Algorithm Selection
-------------------

Diffy supports multiple diff algorithms, each with different performance characteristics:

* **`-a p` (patience)**: Default. Good balance of quality and speed.
* **`-a s` (streaming-hybrid)**: Fastest on large files (2-5x speedup). Uses hash-based anchor detection to skip unchanged regions. Recommended for files >1000 lines.
* **`-a ml` (myers-linear)**: Optimal edit distance, linear space. Standard Myers algorithm.
* **`-a mg` (myers-greedy)**: Optimal edit distance, greedy variant.

Example:

    $ diffy -a s large_file_old.c large_file_new.c    # Use streaming hybrid
    $ diffy -U5 file1.txt file2.txt                    # Unified diff with 5 context lines
    $ diffy -S3 old.cpp new.cpp                        # Side-by-side with 3 context lines

Using diffy with git
--------------------
Copy `extras/diffy-git` to somewhere in your `$PATH`. This wrapper script invokes
`diffy` as a difftool. It allows negative numbers similar to `git show -2`.

    $ git diffy -2


References
----------
    [1] https://blog.jcoglan.com/2017/02/12/the-myers-diff-algorithm-part-1/
