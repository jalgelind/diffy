#include "gui_config.hpp"

#include "config/config.hpp"  // config_get_directory

#include <fmt/format.h>
#include <config_parser/config_parser.hpp>
#include <config_parser/config_parser_utils.hpp>
#include <config_parser/config_serializer.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string
diffy_conf_path() {
    // Allow tests (and sandboxed runs) to redirect the config file.
    if (const char* override = std::getenv("DIFFY_CONF_PATH"); override && *override) {
        return override;
    }
    return fmt::format("{}/diffy.conf", diffy::config_get_directory());
}

void
write_table(diffy::Value& table) {
    std::error_code ec;
    fs::create_directories(diffy::config_get_directory(), ec);
    std::ofstream f(diffy_conf_path(), std::ios::binary);
    if (f) {
        f << diffy::cfg_serialize(table);
    }
}

}  // namespace

void
diffy::gui_settings_load(GuiSettings& settings) {
    ParseResult parse_result;
    Value table;  // default empty table
    cfg_load_file(diffy_conf_path(), parse_result, table);
    if (!table.is_table()) {
        return;  // a malformed diffy.conf: leave defaults, don't clobber it
    }

    bool dirty = false;

    auto sync_str = [&](const char* key, std::string& field) {
        if (auto v = table.lookup_value_by_path(key); v && v->get().is_string()) {
            field = v->get().as_string();
        } else {
            table.set_value_at(key, Value{field});
            dirty = true;
        }
    };
    auto sync_int = [&](const char* key, int64_t& field) {
        if (auto v = table.lookup_value_by_path(key); v && v->get().is_int()) {
            field = v->get().as_int();
        } else {
            table.set_value_at(key, Value{static_cast<int>(field)});
            dirty = true;
        }
    };
    auto sync_bool = [&](const char* key, bool& field) {
        if (auto v = table.lookup_value_by_path(key); v && v->get().is_bool()) {
            field = v->get().as_bool();
        } else {
            table.set_value_at(key, Value{field});
            dirty = true;
        }
    };

    sync_str("gui.default_view", settings.default_view);
    sync_str("gui.theme", settings.theme);
    sync_str("gui.theme_variant", settings.theme_variant);
    sync_str("gui.font_family", settings.font_family);
    sync_int("gui.font_size", settings.font_size);
    sync_bool("gui.word_wrap", settings.word_wrap);
    sync_bool("gui.show_line_numbers", settings.show_line_numbers);
    sync_bool("gui.syntax_highlight", settings.syntax_highlight);
    sync_bool("gui.ignore_whitespace", settings.ignore_whitespace);
    sync_bool("gui.token_granularity", settings.token_granularity);
    sync_int("gui.context_lines", settings.context_lines);
    sync_int("gui.algorithm", settings.algorithm);
    sync_int("gui.tab_width", settings.tab_width);
    sync_int("gui.window_width", settings.window_width);
    sync_int("gui.window_height", settings.window_height);
    sync_int("gui.sidebar_width", settings.sidebar_width);
    sync_int("gui.commits_panel_height", settings.commits_panel_height);
    sync_bool("gui.restore_last_repo", settings.restore_last_repo);

    // Optional [gui.syntax] table: group-name -> "#rrggbb". Read-only (we don't
    // write defaults, so the file is only what the user added).
    if (auto v = table.lookup_value_by_path("gui.syntax"); v && v->get().is_table()) {
        v->get().as_table().for_each([&](const std::string& key, Value& val) {
            if (val.is_string()) {
                settings.syntax_overrides.emplace_back(key, val.as_string());
            }
        });
    }

    if (dirty) {
        write_table(table);
    }
}

void
diffy::gui_settings_save(const GuiSettings& settings) {
    ParseResult parse_result;
    Value table;
    cfg_load_file(diffy_conf_path(), parse_result, table);  // preserve other sections
    if (!table.is_table()) {
        return;
    }

    table.set_value_at("gui.default_view", Value{settings.default_view});
    table.set_value_at("gui.theme", Value{settings.theme});
    table.set_value_at("gui.theme_variant", Value{settings.theme_variant});
    table.set_value_at("gui.font_family", Value{settings.font_family});
    table.set_value_at("gui.font_size", Value{static_cast<int>(settings.font_size)});
    table.set_value_at("gui.word_wrap", Value{settings.word_wrap});
    table.set_value_at("gui.show_line_numbers", Value{settings.show_line_numbers});
    table.set_value_at("gui.syntax_highlight", Value{settings.syntax_highlight});
    table.set_value_at("gui.ignore_whitespace", Value{settings.ignore_whitespace});
    table.set_value_at("gui.token_granularity", Value{settings.token_granularity});
    table.set_value_at("gui.context_lines", Value{static_cast<int>(settings.context_lines)});
    table.set_value_at("gui.algorithm", Value{static_cast<int>(settings.algorithm)});
    table.set_value_at("gui.tab_width", Value{static_cast<int>(settings.tab_width)});
    table.set_value_at("gui.window_width", Value{static_cast<int>(settings.window_width)});
    table.set_value_at("gui.window_height", Value{static_cast<int>(settings.window_height)});
    table.set_value_at("gui.sidebar_width", Value{static_cast<int>(settings.sidebar_width)});
    table.set_value_at("gui.commits_panel_height",
                       Value{static_cast<int>(settings.commits_panel_height)});
    table.set_value_at("gui.restore_last_repo", Value{settings.restore_last_repo});

    write_table(table);
}
