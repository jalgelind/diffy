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
test: ## Run all unit/logic tests: libdiffy + CLI, and the GUI logic tests
	@$(call cli_cmake,$(B)-debug,Debug)
	@$(CMAKE) --build $(B)-debug --target diffy-test
	@ctest --test-dir $(B)-debug --output-on-failure
	@$(MAKE) --no-print-directory gui-test

# GUI logic tests (repo_model, review providers + conformance, diff_bridge,
# text_layout). Slint-free — they link only libgit2, so they build fast — but
# configuring the GUI tree still needs the Rust toolchain (Slint FetchContent).
gui-test: ## Build + run the GUI logic tests (needs Rust + libgit2)
	@$(CMAKE) -S . -B $(B)-gui-tests -G $(GEN) -DCMAKE_BUILD_TYPE=Debug \
		-DDIFFY_BUILD_GUI=ON -DDIFFY_BUILD_CLI=OFF -DDIFFY_BUILD_TESTS=ON
	@$(CMAKE) --build $(B)-gui-tests --target diffy-gui-logic-test
	@ctest --test-dir $(B)-gui-tests -R gui-logic-tests --output-on-failure

# Integration test: diff every fixture pair, apply the unified output with
# `patch`, and verify the result matches. Requires python3 and patch.
integration-test: release ## Diff/patch round-trip over the fixture pairs (needs python3, patch)
	@cd tests && python3 testsuite.py ../$(B)-release/cli/diffy

test-all: test integration-test ## Everything: unit + GUI logic + CLI diff/patch integration

# --- GUI (diffy-gui) -------------------------------------------------------
# Re-sign the built GUI with a STABLE identity when DIFFY_SIGN_ID is set. Every plain
# build is ad-hoc signed with a fresh signature, so macOS treats each launch as a new
# app and re-prompts for Keychain access ("Always Allow" never sticks). Signing with a
# persistent identity (an "Apple Development"/Developer ID cert — see
# `security find-identity -v -p codesigning`) makes the Keychain ACL stick across
# rebuilds, so one "Always Allow" is remembered. No-op when unset.
#   make gui DIFFY_SIGN_ID="Apple Development: you@example.com (TEAMID)"
# NB: we intentionally do NOT apply extras/diffy-gui.entitlements here — the
# keychain-access-groups entitlement it carries needs a provisioning profile, and
# without one AMFI SIGKILLs the app on launch. That entitlement (and SecretStore's
# data-protection-keychain path) only apply to a properly provisioned distribution
# build; a plain signed build uses the legacy keychain, which the stable signature
# already makes prompt-free.
gui_sign = @if [ -n "$(DIFFY_SIGN_ID)" ]; then \
	codesign --force --sign "$(DIFFY_SIGN_ID)" "$(1)/gui/diffy-gui" \
		&& echo "signed $(1)/gui/diffy-gui as '$(DIFFY_SIGN_ID)'"; \
	fi

gui: ## Build the GUI (diffy-gui) in Debug (needs Rust toolchain + libgit2)
	@$(call gui_cmake,$(B)-gui-debug,Debug)
	@$(CMAKE) --build $(B)-gui-debug --target diffy-gui
	$(call gui_sign,$(B)-gui-debug)

gui-release: ## Build the GUI (diffy-gui) in Release
	@$(call gui_cmake,$(B)-gui-release,Release)
	@$(CMAKE) --build $(B)-gui-release --target diffy-gui
	$(call gui_sign,$(B)-gui-release)

gui-run: gui-release ## Build (release) and launch the GUI
	@./$(B)-gui-release/gui/diffy-gui

# Package a self-contained macOS diffy.app (bundled dylibs + icon) from the
# release build. Output: $(B)-gui-release/diffy.app
gui-bundle: gui-release ## Assemble a self-contained macOS diffy.app (dylibs + icon)
	@bash extras/make-macos-app.sh $(B)-gui-release

# Full macOS distribution: build the GUI, assemble the .app, then wrap it in a
# drag-to-Applications diffy.dmg. Outputs both under $(B)-gui-release/.
dist: gui-bundle ## Build the macOS diffy.app and a distributable diffy.dmg
	@bash extras/make-macos-dmg.sh $(B)-gui-release
	@echo "dist: $(B)-gui-release/diffy.app + $(B)-gui-release/diffy.dmg"

clean: ## Remove make-created build trees (leaves the Windows out/ tree)
	rm -rf $(B)-debug $(B)-release $(B)-gui-debug $(B)-gui-release

.PHONY: all help debug release test gui-test integration-test test-all gui gui-release gui-run gui-bundle dist clean
