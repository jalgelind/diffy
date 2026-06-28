# Diffy — convenience Makefile for Unix/WSL/macOS.
#
# Wraps the CMake + Ninja build (the project's primary build; see README) so a
# plain `make` / `make release` works in a POSIX shell. It builds the CLI
# (`diffy`) and the test suite, which only need cmake, ninja and a C++20
# compiler. The GUI is gated behind explicit `gui*` targets because it also
# needs a Rust toolchain (cargo, for Slint) and libgit2.
#
# Build trees live under $(OUT)/<platform>-<config> (e.g. out/linux-release,
# out/macos-gui-debug) so they never clash with the Windows/MSVC tree
# (out/release/build-msvc, from build-windows.cmd) or with each other. The CLI
# binary ends up at e.g. out/linux-release/cli/diffy.

CMAKE ?= cmake
GEN   := Ninja
OUT   ?= out

# Platform prefix from uname (linux, macos, or the lowercased OS name).
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  PLAT := macos
else ifeq ($(UNAME_S),Linux)
  PLAT := linux
else
  PLAT := $(shell echo $(UNAME_S) | tr A-Z a-z)
endif

B := $(OUT)/$(PLAT)

# CLI + tests configuration shared by debug/release.
cli_cmake = $(CMAKE) -S . -B $(1) -G $(GEN) -DCMAKE_BUILD_TYPE=$(2) \
	-DDIFFY_BUILD_CLI=ON -DDIFFY_BUILD_GUI=OFF -DDIFFY_BUILD_TESTS=ON

# GUI configuration: Slint is built from source (needs a Rust toolchain) and
# libgit2 must be available (`apt install libgit2-dev` / `brew install libgit2`).
gui_cmake = $(CMAKE) -S . -B $(1) -G $(GEN) -DCMAKE_BUILD_TYPE=$(2) \
	-DDIFFY_BUILD_GUI=ON -DDIFFY_BUILD_CLI=OFF -DDIFFY_BUILD_TESTS=OFF

all: debug

# Each target reconfigures before building. CMake is idempotent and cheap when
# nothing changed, but this is what makes the build pick up edits to
# CMakeLists/treesitter.cmake (e.g. added grammars) rather than silently
# building against a stale configuration.

# --- CLI (diffy) -----------------------------------------------------------
debug:
	@$(call cli_cmake,$(B)-debug,Debug)
	@$(CMAKE) --build $(B)-debug
	@echo "built: $(B)-debug/cli/diffy"

release:
	@$(call cli_cmake,$(B)-release,Release)
	@$(CMAKE) --build $(B)-release
	@echo "built: $(B)-release/cli/diffy"

# --- Tests -----------------------------------------------------------------
test:
	@$(call cli_cmake,$(B)-debug,Debug)
	@$(CMAKE) --build $(B)-debug --target diffy-test
	@ctest --test-dir $(B)-debug --output-on-failure

# Integration test: diff every fixture pair, apply the unified output with
# `patch`, and verify the result matches. Requires python3 and patch.
integration-test: release
	@cd tests && python3 testsuite.py ../$(B)-release/cli/diffy

# --- GUI (diffy-gui) -------------------------------------------------------
gui:
	@$(call gui_cmake,$(B)-gui-debug,Debug)
	@$(CMAKE) --build $(B)-gui-debug --target diffy-gui

gui-release:
	@$(call gui_cmake,$(B)-gui-release,Release)
	@$(CMAKE) --build $(B)-gui-release --target diffy-gui

# Build (release) and launch the GUI.
gui-run: gui-release
	@./$(B)-gui-release/gui/diffy-gui

# Package a self-contained macOS diffy.app (bundled dylibs + icon) from the
# release build. Output: $(B)-gui-release/diffy.app
gui-bundle: gui-release
	@bash extras/make-macos-app.sh $(B)-gui-release

# Remove only the make-created build trees (leaves the Windows out/ tree alone).
clean:
	rm -rf $(B)-debug $(B)-release $(B)-gui-debug $(B)-gui-release

.PHONY: all debug release test integration-test gui gui-release gui-run gui-bundle clean
