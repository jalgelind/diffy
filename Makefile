build_out := out

all: debug

debug: | $(build_out)/debug/build.ninja
	@ninja -C $(build_out)/debug

$(build_out)/debug/build.ninja:
	@meson setup $(build_out)/debug --buildtype=debug

release: | $(build_out)/release/build.ninja
	@ninja -C $(build_out)/release

$(build_out)/release/build.ninja:
	@meson setup $(build_out)/release --buildtype=release

test: | $(build_out)/debug/build.ninja
	@ninja -C $(build_out)/debug diffy-test
	@$(build_out)/debug/diffy-test

# --- GUI (diffy-gui) -------------------------------------------------------
# The GUI is CMake-only: Slint is built from source (needs a Rust toolchain) and
# libgit2 must be available (e.g. `brew install libgit2`). Separate build dirs
# from the Meson CLI so the two never clash.
gui_debug := build-gui-debug
gui_release := build-gui-release

gui_cmake = cmake -S . -B $(1) -G Ninja -DCMAKE_BUILD_TYPE=$(2) \
	-DDIFFY_BUILD_GUI=ON -DDIFFY_BUILD_CLI=OFF -DDIFFY_BUILD_TESTS=OFF

gui: | $(gui_debug)/build.ninja
	@ninja -C $(gui_debug) diffy-gui

$(gui_debug)/build.ninja:
	@$(call gui_cmake,$(gui_debug),Debug)

gui-release: | $(gui_release)/build.ninja
	@ninja -C $(gui_release) diffy-gui

$(gui_release)/build.ninja:
	@$(call gui_cmake,$(gui_release),Release)

# Build (release) and launch the GUI.
gui-run: gui-release
	@./$(gui_release)/gui/diffy-gui

# Integration test: diff every fixture pair, apply the unified output with
# `patch`, and verify the result matches. Requires python3 and patch.
integration-test: debug
	@cd tests && python3 testsuite.py ../$(build_out)/debug/diffy

clean:
	rm -rf out $(gui_debug) $(gui_release)

$(build_out)/sanitize-address/build.ninja:
	@meson setup $(build_out)/sanitize-address -Db_sanitize=address --buildtype=debug

sanitize-address: | $(build_out)/sanitize-address/build.ninja
	@ninja -C $(build_out)/sanitize-address


# TODO: Not sure what this should look like.
install: release
	cp out/release/diffy $(LOCAL_BIN_DIR)/diffy


.PHONY: all debug debugoptimized release clean test integration-test install gui gui-release gui-run
