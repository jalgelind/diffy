# Diffy — convenience Makefile for Unix/WSL/macOS.
#
# Wraps the CMake + Ninja build (the project's primary build; see README) so a
# plain `make` / `make release` works in a POSIX shell. It builds the CLI
# (`diffy`) and the test suite, which only need cmake, ninja and a C++20
# compiler.
#
# This repository is the diff engine + CLI only; the desktop git client lives in
# the separate diffy-gui repository, which consumes libdiffy as a library.
#
# Build trees live under $(OUT)/<platform>-<config> (e.g. out/linux-release,
# out/macos-debug) so they never clash with the Windows/MSVC tree
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
	-DDIFFY_BUILD_CLI=ON -DDIFFY_BUILD_TESTS=ON

all: debug

# Self-documenting help: every target tagged with a `## <text>` comment is
# listed by `make help`. Keep the tag on the recipe line so it stays in sync.
help: ## Show this help (the default for `make help`)
	@echo "diffy — build targets (run as: make <target>)"
	@echo
	@awk 'BEGIN{FS=":.*## "} /^[a-zA-Z0-9_-]+:.*## / {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo
	@echo "Build trees live under $(OUT)/$(PLAT)-<config>/."

# Each target reconfigures before building. CMake is idempotent and cheap when
# nothing changed, but this is what makes the build pick up edits to
# CMakeLists/treesitter.cmake (e.g. added grammars) rather than silently
# building against a stale configuration.

# --- CLI (diffy) -----------------------------------------------------------
debug: ## Build the CLI (diffy) in Debug — the default target
	@$(call cli_cmake,$(B)-debug,Debug)
	@$(CMAKE) --build $(B)-debug
	@echo "built: $(B)-debug/cli/diffy"

release: ## Build the CLI (diffy) in Release
	@$(call cli_cmake,$(B)-release,Release)
	@$(CMAKE) --build $(B)-release
	@echo "built: $(B)-release/cli/diffy"

# --- Tests -----------------------------------------------------------------
test: ## Run the unit/logic tests (libdiffy + config_parser)
	@$(call cli_cmake,$(B)-debug,Debug)
	@$(CMAKE) --build $(B)-debug --target diffy-test
	@ctest --test-dir $(B)-debug --output-on-failure

# Integration test: diff every fixture pair, apply the unified output with
# `patch`, and verify the result matches. Requires python3 and patch.
integration-test: release ## Diff/patch round-trip over the fixture pairs (needs python3, patch)
	@cd tests && python3 testsuite.py ../$(B)-release/cli/diffy

test-all: test integration-test ## Everything: unit + CLI diff/patch integration

clean: ## Remove make-created build trees (leaves the Windows out/ tree)
	rm -rf $(B)-debug $(B)-release

.PHONY: all help debug release test integration-test test-all clean
