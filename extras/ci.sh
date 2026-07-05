#!/usr/bin/env bash
# Continuous-integration entry point: build the CLI + tests with CMake, run the
# unit/corpus suite, then the diff->patch integration round-trip.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-out/ci}"

echo "==> Configuring ($BUILD_DIR)"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDIFFY_BUILD_CLI=ON -DDIFFY_BUILD_TESTS=ON

echo "==> Building"
ninja -C "$BUILD_DIR" diffy diffy-test

echo "==> Unit + corpus tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "==> Integration tests (diff -> patch round-trip)"
DIFFY_BIN="$(cd "$BUILD_DIR/cli" && pwd)/diffy"
( cd tests && python3 testsuite.py "$DIFFY_BIN" )

echo "==> OK"
