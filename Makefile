# Diffy — convenience Makefile for Unix/WSL.
#
# This wraps the CMake + Ninja build (the project's primary build; see README)
# so a plain `make` / `make release` works in a POSIX shell. It builds the CLI
# (`diffy`) and the test suite, which only need cmake, ninja and a C++20
# compiler. The GUI is gated behind explicit `gui*` targets because it also
# needs a Rust toolchain (cargo, for Slint) and libgit2.
#
# Build dirs live under build-linux/ so they never clash with the Windows/MSVC
# tree in out/ (build-windows.cmd) or each other.

CMAKE   ?= cmake
NINJA   ?= ninja
GEN     := Ninja
BUILD   := build-linux

# CLI + tests configuration shared by debug/release.
cli_cmake = $(CMAKE) -S . -B $(1) -G $(GEN) -DCMAKE_BUILD_TYPE=$(2) \
	-DDIFFY_BUILD_CLI=ON -DDIFFY_BUILD_GUI=OFF -DDIFFY_BUILD_TESTS=ON

all: debug

# Each target reconfigures before building. CMake is idempotent and cheap when
# nothing changed, but this is what makes the build pick up edits to
# CMakeLists/treesitter.cmake (e.g. added grammars) rather than silently
# building against a stale configuration.

# --- CLI (diffy) -----------------------------------------------------------
debug:
	@$(call cli_cmake,$(BUILD)/debug,Debug)
	@$(CMAKE) --build $(BUILD)/debug

release:
	@$(call cli_cmake,$(BUILD)/release,Release)
	@$(CMAKE) --build $(BUILD)/release

# --- Tests -----------------------------------------------------------------
test:
	@$(call cli_cmake,$(BUILD)/debug,Debug)
	@$(CMAKE) --build $(BUILD)/debug --target diffy-test
	@ctest --test-dir $(BUILD)/debug --output-on-failure

# Integration test: diff every fixture pair, apply the unified output with
# `patch`, and verify the result matches. Requires python3 and patch.
integration-test: release
	@cd tests && python3 testsuite.py ../$(BUILD)/release/cli/diffy

# --- GUI (diffy-gui) -------------------------------------------------------
# CMake-only: Slint is built from source (needs a Rust toolchain) and libgit2
# must be available (e.g. `apt install libgit2-dev` / `brew install libgit2`).
# Separate build dirs from the CLI so the two never clash.
gui_cmake = $(CMAKE) -S . -B $(1) -G $(GEN) -DCMAKE_BUILD_TYPE=$(2) \
	-DDIFFY_BUILD_GUI=ON -DDIFFY_BUILD_CLI=OFF -DDIFFY_BUILD_TESTS=OFF

gui:
	@$(call gui_cmake,$(BUILD)/gui-debug,Debug)
	@$(CMAKE) --build $(BUILD)/gui-debug --target diffy-gui

gui-release:
	@$(call gui_cmake,$(BUILD)/gui-release,Release)
	@$(CMAKE) --build $(BUILD)/gui-release --target diffy-gui

# Build (release) and launch the GUI.
gui-run: gui-release
	@./$(BUILD)/gui-release/gui/diffy-gui

clean:
	rm -rf $(BUILD)

.PHONY: all debug release test integration-test gui gui-release gui-run clean
