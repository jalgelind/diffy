#!/usr/bin/env python3
"""Differential diff test over a real git corpus.

For every text file modified across the last N commits of a git repo, run
diffy's unified diff of (parent blob -> commit blob) and check:

  1. RECONSTRUCTION (correctness, hard fail): applying diffy's unified diff to
     the old blob with `patch` must reproduce the new blob byte-for-byte. This
     exercises the whole line-diff pipeline end to end -- patience/Myers,
     ALG-1 anchoring, ALG-2 indent heuristic, hunk composition and unified
     serialisation -- on real-world edits, not toy fixtures. A single mismatch
     means diffy emitted a diff that does not describe the change: a bug.

  2. QUALITY (ALG-1/ALG-2 guard, soft unless egregious): diffy's changed-line
     count is compared to `git diff`'s on the same blob pair. ALG-1/ALG-2 exist
     to make diffs *smaller / better placed*, so diffy should be within a
     generous ratio of git. We only FAIL when diffy is dramatically worse
     (> MAX_RATIO x git over the whole corpus), which would signal an anchoring
     regression; per-file wobble is expected and only reported.

Usage: python3 difftest_corpus.py <path-to-diffy> [--repo DIR] [--commits N]
Exits non-zero on any reconstruction failure or an egregious quality ratio.
"""
import argparse
import os
import subprocess
import sys
import tempfile

MAX_RATIO = 2.0  # corpus-wide diffy_changed / git_changed ceiling


def git(repo, *args, binary=False):
    out = subprocess.run(["git", "-C", repo, *args], capture_output=True)
    if out.returncode != 0:
        return None
    return out.stdout if binary else out.stdout.decode("utf-8", "replace")


def is_text(blob):
    # Same cheap heuristic diffy/git use: a NUL byte in the first 8k => binary.
    return b"\x00" not in blob[:8192]


def changed_lines(unified):
    # Count +/- content lines, excluding the ---/+++ file headers.
    n = 0
    for line in unified.splitlines():
        if line[:3] in ("---", "+++"):
            continue
        if line[:1] in ("+", "-"):
            n += 1
    return n


def collect_pairs(repo, commits):
    """Yield (path, old_bytes, new_bytes) for text files modified in the range."""
    revs = git(repo, "rev-list", "--no-merges", f"-n{commits}", "HEAD")
    if not revs:
        return
    for sha in revs.split():
        names = git(repo, "diff-tree", "--no-commit-id", "--name-only", "-r",
                    "--diff-filter=M", sha)
        if not names:
            continue
        for path in names.splitlines():
            if not path.strip():
                continue
            old = git(repo, "show", f"{sha}^:{path}", binary=True)
            new = git(repo, "show", f"{sha}:{path}", binary=True)
            if old is None or new is None or old == new:
                continue
            if not (is_text(old) and is_text(new)):
                continue
            yield path, old, new


def reconstruct(diffy, old, new):
    """Return (ok, diffy_changed, git_changed). ok=False => patch mismatch."""
    with tempfile.TemporaryDirectory() as d:
        a, b, recon = (os.path.join(d, n) for n in ("a", "b", "recon"))
        for p, data in ((a, old), (b, new), (recon, old)):
            with open(p, "wb") as f:
                f.write(data)

        dp = subprocess.run([diffy, "-u", "--color=never", a, b], capture_output=True)
        unified = dp.stdout
        if not unified:
            # No diff emitted for a genuine change is itself a failure.
            return old == new, 0, 0

        pr = subprocess.run(["patch", "-s", recon], input=unified, capture_output=True)
        with open(recon, "rb") as f:
            ok = pr.returncode == 0 and f.read() == new

        gd = subprocess.run(
            ["git", "diff", "--no-index", "--no-color", a, b], capture_output=True)
        return ok, changed_lines(unified.decode("utf-8", "replace")), \
            changed_lines(gd.stdout.decode("utf-8", "replace"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("diffy")
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--commits", type=int, default=60)
    args = ap.parse_args()

    n = fails = 0
    diffy_total = git_total = 0
    for path, old, new in collect_pairs(args.repo, args.commits):
        n += 1
        ok, dc, gc = reconstruct(args.diffy, old, new)
        diffy_total += dc
        git_total += gc
        if not ok:
            fails += 1
            print(f"  RECONSTRUCT FAIL: {path}")
        elif gc and dc > gc * 3:  # per-file egregious: worth surfacing
            print(f"  note: {path}: diffy {dc} vs git {gc} changed lines")

    if n == 0:
        print("corpus: no text file pairs found (shallow clone?) -- skipped")
        return 0

    ratio = diffy_total / git_total if git_total else 1.0
    print(f"corpus: {n} file pairs, {fails} reconstruct failures; "
          f"changed-line ratio diffy/git = {ratio:.2f} "
          f"({diffy_total} vs {git_total})")

    bad_ratio = git_total > 0 and ratio > MAX_RATIO
    if bad_ratio:
        print(f"  FAIL: corpus diff is {ratio:.2f}x git's size (> {MAX_RATIO}) "
              "-- possible anchoring/heuristic regression")
    return 1 if (fails or bad_ratio) else 0


if __name__ == "__main__":
    sys.exit(main())
