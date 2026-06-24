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

# Integration test: diff every fixture pair, apply the unified output with
# `patch`, and verify the result matches. Requires python3 and patch.
integration-test: debug
	@cd tests && python3 testsuite.py ../$(build_out)/debug/diffy

clean:
	rm -rf out

$(build_out)/sanitize-address/build.ninja:
	@meson setup $(build_out)/sanitize-address -Db_sanitize=address --buildtype=debug

sanitize-address: | $(build_out)/sanitize-address/build.ninja
	@ninja -C $(build_out)/sanitize-address


# TODO: Not sure what this should look like.
install: release
	cp out/release/diffy $(LOCAL_BIN_DIR)/diffy


.PHONY: all debug debugoptimized release clean test integration-test install
