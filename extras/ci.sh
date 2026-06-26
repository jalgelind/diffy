#!/usr/bin/env bash
# Continuous-integration entry point: build the CLI + tests with CMake, run the
# unit/corpus suite, then the diff->patch integration round-trip.
#
# The GUI is intentionally excluded here: building Slint from source pulls a Rust
# toolchain and is too heavy for a quick CI gate. Add `-DDIFFY_BUILD_GUI=ON` and a
# separate job if/when GUI CI is wanted.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build-ci}"

echo "==> Configuring ($BUILD_DIR)"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDIFFY_BUILD_CLI=ON -DDIFFY_BUILD_TESTS=ON -DDIFFY_BUILD_GUI=OFF

echo "==> Building"
ninja -C "$BUILD_DIR" diffy diffy-test

echo "==> Unit + corpus tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "==> Integration tests (diff -> patch round-trip)"
DIFFY_BIN="$(cd "$BUILD_DIR/cli" && pwd)/diffy"
( cd tests && python3 testsuite.py "$DIFFY_BIN" )

echo "==> OK"
