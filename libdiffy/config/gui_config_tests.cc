// Round-trip tests for the [gui] settings table. These redirect the config
// file to a temp path via DIFFY_CONF_PATH so they never touch the real
// diffy.conf.

#include "config/gui_config.hpp"

#include <doctest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace diffy;
namespace fs = std::filesystem;

namespace {

// Scoped temp config file + DIFFY_CONF_PATH override, cleaned up on destruction.
struct ScopedConf {
    std::string path;
    ScopedConf() {
        path = (fs::temp_directory_path() / "diffy_gui_cfg_roundtrip_test.conf").string();
        std::error_code ec;
        fs::remove(path, ec);
#ifdef _WIN32
        _putenv_s("DIFFY_CONF_PATH", path.c_str());
#else
        ::setenv("DIFFY_CONF_PATH", path.c_str(), 1);
#endif
    }
    ~ScopedConf() {
#ifdef _WIN32
        _putenv_s("DIFFY_CONF_PATH", "");
#else
        ::unsetenv("DIFFY_CONF_PATH");
#endif
        std::error_code ec;
        fs::remove(path, ec);
    }
};

}  // namespace

TEST_CASE("gui settings: save then load round-trips every field") {
    ScopedConf conf;

    GuiSettings out;
    out.default_view = "unified";
    out.theme = "theme_nord";
    out.theme_variant = "light";
    out.font_family = "JetBrains Mono";
    out.font_size = 16;
    out.word_wrap = true;
    out.show_line_numbers = false;
    out.syntax_highlight = false;
    out.ignore_whitespace = true;
    out.token_granularity = false;
    out.context_lines = 7;
    out.algorithm = 2;
    out.tab_width = 8;
    out.window_width = 1600;
    out.window_height = 900;
    out.sidebar_width = 360;
    out.commits_panel_height = 220;
    out.restore_last_repo = false;

    gui_settings_save(out);

    GuiSettings in;  // defaults
    gui_settings_load(in);

    CHECK(in.default_view == "unified");
    CHECK(in.theme == "theme_nord");
    CHECK(in.theme_variant == "light");
    CHECK(in.font_family == "JetBrains Mono");
    CHECK(in.font_size == 16);
    CHECK(in.word_wrap == true);
    CHECK(in.show_line_numbers == false);
    CHECK(in.syntax_highlight == false);
    CHECK(in.ignore_whitespace == true);
    CHECK(in.token_granularity == false);
    CHECK(in.context_lines == 7);
    CHECK(in.algorithm == 2);
    CHECK(in.tab_width == 8);
    CHECK(in.window_width == 1600);
    CHECK(in.window_height == 900);
    CHECK(in.sidebar_width == 360);
    CHECK(in.commits_panel_height == 220);
    CHECK(in.restore_last_repo == false);
}

TEST_CASE("gui settings: load of a missing file yields defaults and writes them") {
    ScopedConf conf;

    GuiSettings in;
    gui_settings_load(in);  // file doesn't exist yet

    // Defaults survive, and the load wrote a file we can read back.
    CHECK(in.default_view == "side-by-side");
    CHECK(in.tab_width == 4);
    CHECK(fs::exists(conf.path));

    GuiSettings again;
    gui_settings_load(again);
    CHECK(again.default_view == "side-by-side");
    CHECK(again.font_size == in.font_size);
}
