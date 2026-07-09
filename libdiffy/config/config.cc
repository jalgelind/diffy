#include "config.hpp"

#include <highlight/highlight_group.hpp>
#include <highlight/highlight_palette.hpp>
#include <highlight/language.hpp>
#include <output/column_view.hpp>
#include <util/color.hpp>

#include <fmt/format.h>
#include <sago/platform_folders.h>
#include <config_parser/config_parser.hpp>
#include <config_parser/config_serializer.hpp>
#include <config_parser/config_parser_utils.hpp>

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

static std::string config_doc_theme = R"foo(# Theme configuration
# 
# Customize colors using the `color_map` table for global mappings
# or changing the color style of each specific theme item.
#
# You can re-map these colors in `color_map` below. Supported
# values are the palette names are as follow:
#
# RGB hex colors:
#   '#RGB' and '#RRGGBB'. I.e '#F00' or '#FF0000' for bright red.
#
# 256 color palette (see https://www.ditig.com/256-colors-cheat-sheet):
#   'P<palette-index>', I.e 'P196' for the color known as "Red1"
#
# 16 color palette SGR colors:
#   black, red, green, yellow, blue, magenta, cyan, light_gray,
#   dark_gray, light_red, light_green, light_yellow, light_blue,
#   light_magenta, light_cyan, white
#
# Available attributes:
#   'bold', 'dim', 'italic', 'underline',
#   'blink', 'inverse', 'hidden', 'strikethrough'
#
)foo";

static std::string color_map_comment = R"foo(# Custom color map with aliases
# E.g:
#   red = '#FF1111'
#   background = 'black')foo";

static std::string config_doc_general = R"foo(# General configuration for ´diffy´
#
# Configure default options. These can be overriden with command-line arguments.
#
# To use another theme, update `theme` to point to another theme file; i.e:
#   theme = 'custom_theme'  # load custom_theme.conf
#
# Bundled themes (created in the config directory on first run):
#   theme_default, theme_dracula, theme_nord, theme_solarized_dark,
#   theme_github_light
#
# Syntax highlighting grammars are loaded from 'grammars/' next to the diffy
# executable, or dropped into '<this directory>/grammars/' (a tree-sitter
# grammar library plus a <name>.scm highlights query). Map file extensions or
# exact filenames to a grammar with:
#   [highlight]
#   extensions = { cpp = ['.tpp', '.ixx'], zig = '.zig' }
#
)foo";

// Bundled themes written alongside the default theme on first-run setup. Each is
// fully self-contained — it sets both a foreground and a background on every
// element (and a base `style.background`) — so it renders identically regardless
// of the terminal's default colors, unlike the foreground-only default theme.
// Stored as raw .conf text because hex colors cannot round-trip through
// TermStyle::to_value() (which only knows palette names); the load path parses
// hex fine.
static std::string theme_dracula_conf = R"foo(# Dracula — popular dark theme (https://draculatheme.com).
# Self-contained: every element sets fg + bg, so it looks the same on any
# terminal regardless of your default colors.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#f8f8f2', bg = '#282a36', attr = [] }
header              = { fg = '#f8f8f2', bg = '#44475a', attr = ['bold', 'underline'] }
delete_line         = { fg = '#f8f8f2', bg = '#5a2e3c', attr = [] }
delete_token        = { fg = '#ff5555', bg = '#7d3346', attr = ['bold'] }
delete_line_number  = { fg = '#ff5555', bg = '#5a2e3c', attr = [] }
insert_line         = { fg = '#f8f8f2', bg = '#2e4a38', attr = [] }
insert_token        = { fg = '#50fa7b', bg = '#326b47', attr = ['bold'] }
insert_line_number  = { fg = '#50fa7b', bg = '#2e4a38', attr = [] }
common_line         = { fg = '#f8f8f2', bg = '#282a36', attr = [] }
common_line_number  = { fg = '#6272a4', bg = '#282a36', attr = [] }
frame               = { fg = '#6272a4', bg = '#282a36', attr = [] }
empty_cell          = { fg = '#6272a4', bg = '#282a36', attr = [] }

# Syntax palette (tree-sitter groups). Omitted groups fall back to the built-in
# default for the theme's light/dark.
[syntax]
comment          = '#6272a4'
keyword          = '#ff79c6'
operator         = '#ff79c6'
punctuation      = '#f8f8f2'
string           = '#f1fa8c'
escape           = '#ff79c6'
number           = '#bd93f9'
boolean          = '#bd93f9'
constant         = '#bd93f9'
constant_builtin = '#bd93f9'
function         = '#50fa7b'
method           = '#50fa7b'
constructor      = '#8be9fd'
type             = '#8be9fd'
type_builtin     = '#8be9fd'
variable         = '#f8f8f2'
parameter        = '#ffb86c'
property         = '#f8f8f2'
namespace        = '#8be9fd'
tag              = '#ff79c6'
attribute        = '#50fa7b'
)foo";

static std::string theme_nord_conf = R"foo(# Nord — popular arctic dark theme (https://www.nordtheme.com).
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#d8dee9', bg = '#2e3440', attr = [] }
header              = { fg = '#eceff4', bg = '#434c5e', attr = ['bold', 'underline'] }
delete_line         = { fg = '#d8dee9', bg = '#4f343f', attr = [] }
delete_token        = { fg = '#bf616a', bg = '#6e404f', attr = ['bold'] }
delete_line_number  = { fg = '#bf616a', bg = '#4f343f', attr = [] }
insert_line         = { fg = '#d8dee9', bg = '#3a4d39', attr = [] }
insert_token        = { fg = '#a3be8c', bg = '#4a6b48', attr = ['bold'] }
insert_line_number  = { fg = '#a3be8c', bg = '#3a4d39', attr = [] }
common_line         = { fg = '#d8dee9', bg = '#2e3440', attr = [] }
common_line_number  = { fg = '#4c566a', bg = '#2e3440', attr = [] }
frame               = { fg = '#434c5e', bg = '#2e3440', attr = [] }
empty_cell          = { fg = '#4c566a', bg = '#2e3440', attr = [] }

# Syntax palette (tree-sitter groups). Omitted groups fall back to the built-in
# default for the theme's light/dark.
[syntax]
comment          = '#616e88'
keyword          = '#81a1c1'
operator         = '#81a1c1'
punctuation      = '#eceff4'
string           = '#a3be8c'
escape           = '#ebcb8b'
number           = '#b48ead'
boolean          = '#81a1c1'
constant         = '#b48ead'
constant_builtin = '#81a1c1'
function         = '#88c0d0'
method           = '#88c0d0'
constructor      = '#8fbcbb'
type             = '#8fbcbb'
type_builtin     = '#8fbcbb'
variable         = '#d8dee9'
parameter        = '#d8dee9'
property         = '#d8dee9'
namespace        = '#8fbcbb'
tag              = '#81a1c1'
attribute        = '#8fbcbb'
)foo";

static std::string theme_solarized_dark_conf =
    R"foo(# Solarized Dark — popular low-contrast theme (Ethan Schoonover).
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#839496', bg = '#002b36', attr = [] }
header              = { fg = '#93a1a1', bg = '#073642', attr = ['bold', 'underline'] }
delete_line         = { fg = '#839496', bg = '#46282c', attr = [] }
delete_token        = { fg = '#dc322f', bg = '#6b333a', attr = ['bold'] }
delete_line_number  = { fg = '#dc322f', bg = '#46282c', attr = [] }
insert_line         = { fg = '#839496', bg = '#25462e', attr = [] }
insert_token        = { fg = '#859900', bg = '#33663f', attr = ['bold'] }
insert_line_number  = { fg = '#859900', bg = '#25462e', attr = [] }
common_line         = { fg = '#839496', bg = '#002b36', attr = [] }
common_line_number  = { fg = '#586e75', bg = '#002b36', attr = [] }
frame               = { fg = '#586e75', bg = '#002b36', attr = [] }
empty_cell          = { fg = '#586e75', bg = '#002b36', attr = [] }

# Syntax palette (tree-sitter groups). Omitted groups fall back to the built-in
# default for the theme's light/dark.
[syntax]
comment          = '#586e75'
keyword          = '#859900'
operator         = '#859900'
punctuation      = '#839496'
string           = '#2aa198'
escape           = '#dc322f'
number           = '#d33682'
boolean          = '#b58900'
constant         = '#d33682'
constant_builtin = '#cb4b16'
function         = '#268bd2'
method           = '#268bd2'
constructor      = '#b58900'
type             = '#b58900'
type_builtin     = '#b58900'
variable         = '#268bd2'
parameter        = '#268bd2'
property         = '#268bd2'
namespace        = '#b58900'
tag              = '#268bd2'
attribute        = '#93a1a1'
)foo";

static std::string theme_github_light_conf =
    R"foo(# GitHub Light — popular light theme. A light background makes this a good
# example of a theme that is unaffected by (typically dark) terminal defaults.
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
# On a light theme, recoloring the changed-token *foreground* clashes with the
# dark context text (you get a black/green mix). GitHub instead keeps all text
# dark and highlights changed words with a stronger *background*, so the tokens
# below set a bg and keep the dark foreground.
background          = { fg = '#1f2328', bg = '#ffffff', attr = [] }
header              = { fg = '#0969da', bg = '#ddf4ff', attr = ['bold', 'underline'] }
delete_line         = { fg = '#1f2328', bg = '#ffebe9', attr = [] }
delete_token        = { fg = '#1f2328', bg = '#ffc4c2', attr = ['bold'] }
delete_line_number  = { fg = '#cf222e', bg = '#ffd7d5', attr = [] }
insert_line         = { fg = '#1f2328', bg = '#e6ffec', attr = [] }
insert_token        = { fg = '#1f2328', bg = '#abf2bc', attr = ['bold'] }
insert_line_number  = { fg = '#116329', bg = '#ccffd8', attr = [] }
common_line         = { fg = '#1f2328', bg = '#ffffff', attr = [] }
common_line_number  = { fg = '#6e7781', bg = '#ffffff', attr = [] }
frame               = { fg = '#d0d7de', bg = '#ffffff', attr = [] }
empty_cell          = { fg = '#6e7781', bg = '#ffffff', attr = [] }

# Syntax palette (tree-sitter groups). Omitted groups fall back to the built-in
# default for the theme's light/dark.
[syntax]
comment          = '#6e7781'
keyword          = '#cf222e'
operator         = '#0550ae'
punctuation      = '#1f2328'
string           = '#0a3069'
escape           = '#0a3069'
number           = '#0550ae'
boolean          = '#0550ae'
constant         = '#0550ae'
constant_builtin = '#0550ae'
function         = '#8250df'
method           = '#8250df'
constructor      = '#8250df'
type             = '#8250df'
type_builtin     = '#cf222e'
variable         = '#953800'
parameter        = '#1f2328'
property         = '#953800'
namespace        = '#8250df'
tag              = '#116329'
attribute        = '#0550ae'
)foo";

enum class ConfigVariableType {
    Bool,
    Int,
    String,
    Color,
};

std::string
diffy::config_get_directory() {
    return fmt::format("{}/diffy", sago::getConfigHome());
}

std::vector<std::pair<std::string, std::string>>
diffy::config_bundled_themes() {
    return {
        {"theme_dracula", theme_dracula_conf},
        {"theme_nord", theme_nord_conf},
        {"theme_solarized_dark", theme_solarized_dark_conf},
        {"theme_github_light", theme_github_light_conf},
    };
}

// Print a single "Creating initial configuration" header before the first
// created file (lazily, so nothing prints when everything already exists), then
// list each created file's name indented under it.
static void
announce_created_file(const std::string& config_root, const std::string& path) {
    // Diagnostics go to stderr so first-run output never corrupts stdout (a
    // piped unified diff must stay a valid patch; golden snapshots stay stable).
    static bool header_printed = false;
    if (!header_printed) {
        fmt::print(stderr, "Creating initial configuration in {}:\n", config_root);
        header_printed = true;
    }
    fmt::print(stderr, "  {}\n", std::filesystem::path(path).filename().string());
}

// Write the bundled example themes into the config directory, skipping any that
// already exist so user edits are never clobbered.
static void
config_write_bundled_themes(const std::string& config_root) {
    std::error_code ec;
    std::filesystem::create_directories(config_root, ec);
    for (const auto& [name, content] : diffy::config_bundled_themes()) {
        const std::string path = fmt::format("{}/{}.conf", config_root, name);
        if (std::filesystem::exists(path))
            continue;
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            fmt::print(stderr, "warning: could not write bundled theme '{}'\n", path);
            continue;
        }
        announce_created_file(config_root, path);
        fwrite(content.c_str(), content.size(), 1, f);
        fclose(f);
    }
}

enum class ConfigLoadResult {
    Ok,
    Invalid,
    DoesNotExist,
};

ConfigLoadResult
config_load_file(const std::string& config_path,
                 diffy::Value& config_table,
                 diffy::ParseResult& load_result) {
    ConfigLoadResult result = ConfigLoadResult::Ok;

    if (cfg_load_file(config_path, load_result, config_table)) {
        if (config_table.is_table()) {
            result = ConfigLoadResult::Ok;
        } else {
            result = ConfigLoadResult::Invalid;
        }
    } else {
        if (load_result.kind == diffy::ParseErrorKind::File) {
            result = ConfigLoadResult::DoesNotExist;
        } else {
            result = ConfigLoadResult::Invalid;
        }
    }
    return result;
}

static void
config_save(const std::string& config_root, const std::string& config_name, diffy::Value& config_value) {
    std::vector<std::string> required_dirs;
    std::filesystem::path current{config_root};
    while (current.has_root_directory() && !std::filesystem::exists(current)) {
        if (current == std::filesystem::current_path().root_path())
            break;
        required_dirs.push_back(current.string());
        current = current.parent_path();
    }

    for (auto it = required_dirs.rbegin(); it != required_dirs.rend(); ++it) {
        if (!std::filesystem::exists(*it)) {
            std::filesystem::create_directory(*it);
        }
    }

    // Write to a temp file then rename over the target, so a crash mid-write can't
    // truncate/corrupt diffy.conf (shared with the GUI). Check the write, too.
    const std::string tmp = config_name + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        fmt::print(stderr, "Failed to open '{}' for writing.\n", tmp.c_str());
        fmt::print(stderr, "   errno ({}) = {}\n", errno, strerror(errno));
        return;
    }

    std::string serialized = cfg_serialize(config_value);
    bool ok = serialized.empty() || fwrite(serialized.data(), 1, serialized.size(), f) == serialized.size();
    if (fflush(f) != 0)
        ok = false;
    if (fclose(f) != 0)
        ok = false;

    std::error_code ec;
    if (!ok) {
        fmt::print(stderr, "Failed to write '{}'.\n", config_name.c_str());
        std::filesystem::remove(tmp, ec);
        return;
    }
    std::filesystem::rename(tmp, config_name, ec);
    if (ec) {
        fmt::print(stderr, "Failed to replace '{}': {}\n", config_name.c_str(), ec.message());
        std::filesystem::remove(tmp, ec);
    }
}

using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;

static void
config_apply_options(diffy::Value& config, const OptionVector& options) {
    for (const auto& [path, type, ptr] : options) {
        // Do we have a value for this option in the config we loaded?
        if (auto stored_value = config.lookup_value_by_path(path); stored_value) {
            // Yes. So we take the value and write it into our settings struct.
            auto& v = stored_value->get();
            switch (type) {
                case ConfigVariableType::Bool: {
                    if (v.is_bool()) {
                        *((bool*) ptr) = v.as_bool();
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected bool)\n", path);
                    }
                } break;
                case ConfigVariableType::Int: {
                    if (v.is_int()) {
                        *((int64_t*) ptr) = (int64_t) v.as_int();
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected int)\n", path);
                    }
                } break;
                case ConfigVariableType::String: {
                    if (v.is_string()) {
                        *((std::string*) ptr) = v.as_string();
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected string)\n", path);
                    }
                } break;
                case ConfigVariableType::Color: {
                    if (v.is_table()) {
                        auto style = diffy::TermStyle::parse_value(v.as_table());
                        if (style) {
                            *((diffy::TermStyle*) ptr) = *style;
                        }
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected table)\n", path);
                    }
                } break;
            }
        } else {
            // No such setting in the stored file, so we store the default value
            // from the struct.
            switch (type) {
                case ConfigVariableType::Bool: {
                    diffy::Value v{diffy::Value::Bool{*(bool*) ptr}};
                    config.set_value_at(path, v);
                } break;
                case ConfigVariableType::Int: {
                    int value = *(int64_t*) ptr;
                    diffy::Value v{diffy::Value::Int{value}};
                    config.set_value_at(path, v);
                } break;
                case ConfigVariableType::String: {
                    diffy::Value v{diffy::Value::String{*(std::string*) ptr}};
                    config.set_value_at(path, v);
                } break;
                case ConfigVariableType::Color: {
                    diffy::TermStyle* style = (diffy::TermStyle*) ptr;
                    config.set_value_at(path, style->to_value());
                }
            }
        }
    }
}

void
diffy::config_apply_options(diffy::ProgramOptions& program_options) {
    const std::string config_file_name = "diffy.conf";
    const std::string config_root = diffy::config_get_directory();
    const std::string config_path = fmt::format("{}/{}", config_root, config_file_name);

    bool flush_config_to_disk = false;

    ParseResult config_parse_result;
    Value config_file_table_value;
    switch (config_load_file(config_path, config_file_table_value, config_parse_result)) {
        case ConfigLoadResult::Ok: {
            // yay!
        } break;
        case ConfigLoadResult::Invalid: {
            fmt::print(stderr, "error: {}\n\twhile parsing: {}\n", config_parse_result.error, config_path);
        } break;
        case ConfigLoadResult::DoesNotExist: {
            announce_created_file(config_root, config_path);
            flush_config_to_disk = true;
        } break;
    };

    // Sync up the rest of the configuration with the options structs
    using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;

    std::string algorithm = "patience";

    // clang-format off
    const OptionVector options = {
       { "general.default_algorithm",   ConfigVariableType::String, &algorithm },
       { "general.theme",               ConfigVariableType::String, &program_options.theme },
       { "general.context_lines",       ConfigVariableType::Int,    &program_options.context_lines},
       { "general.ignore_line_endings", ConfigVariableType::Bool,   &program_options.ignore_line_endings },
       { "general.ignore_whitespace",   ConfigVariableType::Bool,   &program_options.ignore_whitespace },
    };
    // clang-format on

    config_apply_options(config_file_table_value, options);

    if (auto algo = algo_from_string(algorithm); algo != Algo::kInvalid) {
        program_options.algorithm = algo;
    }

    config_apply_highlight_overrides();

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        if (config_file_table_value["general"].key_comments.empty()) {
            config_file_table_value["general"].key_comments.push_back(config_doc_general);
        }
        config_save(config_root, config_path, config_file_table_value);
    }
}

void
diffy::config_apply_highlight_overrides() {
    // Honor the DIFFY_CONF_PATH test/sandbox override when locating diffy.conf.
    std::string config_path;
    if (const char* override = std::getenv("DIFFY_CONF_PATH"); override && *override) {
        config_path = override;
    } else {
        config_path = fmt::format("{}/diffy.conf", config_get_directory());
    }

    ParseResult parse_result;
    Value table;
    if (!cfg_load_file(config_path, parse_result, table) || !table.is_table()) {
        return;
    }
    auto section = table.lookup_value_by_path("highlight.extensions");
    if (!section || !section->get().is_table()) {
        return;
    }

    std::vector<std::pair<std::string, Language>> patterns;
    section->get().as_table().for_each([&](const std::string& lang, Value& v) {
        if (v.is_string()) {
            patterns.emplace_back(v.as_string(), lang);
        } else if (v.is_array()) {
            for (auto& entry : v.as_array()) {
                if (entry.is_string()) {
                    patterns.emplace_back(entry.as_string(), lang);
                }
            }
        } else {
            fmt::print(stderr,
                       "warning: config value at 'highlight.extensions.{}' is invalid "
                       "(expected string or array of strings)\n",
                       lang);
        }
    });
    language_set_overrides(std::move(patterns));
}

void
diffy::config_apply_theme(const std::string& theme,
                          diffy::ColumnViewCharacters& cv_char_opts,
                          diffy::ColumnViewSettings& cv_view_opts,
                          diffy::ColumnViewTextStyle& cv_style_opts,
                          diffy::ColumnViewTextStyleEscapeCodes& cv_style_escape_codes) {
    const std::string config_file_name = fmt::format("{}.conf", theme);
    const std::string config_root = diffy::config_get_directory();
    const std::string config_path = fmt::format("{}/{}", config_root, config_file_name);

    bool flush_config_to_disk = false;

    ParseResult config_parse_result;
    Value config_file_table_value;
    switch (config_load_file(config_path, config_file_table_value, config_parse_result)) {
        case ConfigLoadResult::Ok: {
            // yay!
        } break;
        case ConfigLoadResult::Invalid: {
            fmt::print(stderr, "error: {}\n\twhile parsing: {}\n", config_parse_result.error, config_path);
        } break;
        case ConfigLoadResult::DoesNotExist: {
            if (theme == "theme_default") {
                announce_created_file(config_root, config_path);
                flush_config_to_disk = true;
            }
        } break;
    };

    // Ensure the bundled example themes exist (idempotent; skips any already on
    // disk, so user edits are never clobbered). Done on every run, not just
    // first-run, so existing installs pick them up too. Placed after the default
    // theme so the created-files list reads default-first.
    config_write_bundled_themes(config_root);

    if (!config_file_table_value.lookup_value_by_path("settings")) {
        config_file_table_value["settings"] = {Value::Table{}};
    }

    if (auto value = config_file_table_value.lookup_value_by_path("style.empty_line"); value != std::nullopt) {
        config_file_table_value.set_value_at("style.empty_cell", *value);
        config_file_table_value["style"]["empty_cell"].key_comments.push_back(
            "// 1.11 migration: 'style.empty_line' renamed to 'style.empty_cell'");
        config_file_table_value["style"].as_table().remove("empty_line");
        flush_config_to_disk = true;
    }

    // Update the color table
    {
        if (config_file_table_value["color_map"].key_comments.empty()) {
            config_file_table_value["color_map"].key_comments.push_back(color_map_comment);
        }
        if (!config_file_table_value.lookup_value_by_path("color_map.red")) {
            config_file_table_value.set_value_at("color_map.red", {"red"});
        }

        auto& color_values = config_file_table_value.lookup_value_by_path("color_map")->get();

        if (color_values.is_table()) {
            color_values.as_table().for_each([&](const std::string& key, Value& value) {
                if (value.is_string()) {
                    auto term_color = TermColor::parse_value(value);
                    if (term_color) {
                        color_map_set(key, *term_color);
                    }
                }
            });
        }
    }

    // Sync up the rest of the configuration with the options structs
    using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;
    // clang-format off
    const OptionVector options = {
        // side-by-side settings
        { "settings.word_wrap",                    ConfigVariableType::Bool, &cv_view_opts.word_wrap},
        { "settings.show_line_numbers",            ConfigVariableType::Bool, &cv_view_opts.show_line_numbers},
        { "settings.context_colored_line_numbers", ConfigVariableType::Bool, &cv_view_opts.context_colored_line_numbers},
        { "settings.line_number_align_right",      ConfigVariableType::Bool, &cv_view_opts.line_number_align_right},

        // side-by-side theme
        { "chars.column_separator",         ConfigVariableType::String, &cv_char_opts.column_separator },
        { "chars.edge_separator",           ConfigVariableType::String, &cv_char_opts.edge_separator },
        { "chars.tab_replacement",          ConfigVariableType::String, &cv_char_opts.tab_replacement },
        { "chars.cr_replacement",           ConfigVariableType::String, &cv_char_opts.cr_replacement },
        { "chars.lf_replacement",           ConfigVariableType::String, &cv_char_opts.lf_replacement },
        { "chars.crlf_replacement",         ConfigVariableType::String, &cv_char_opts.crlf_replacement },
        { "chars.space_replacement",        ConfigVariableType::String, &cv_char_opts.space_replacement },

        // side-by-side color style
        { "style.background",               ConfigVariableType::Color,  &cv_style_opts.background },
        { "style.header",                   ConfigVariableType::Color,  &cv_style_opts.header },
        { "style.delete_line",              ConfigVariableType::Color,  &cv_style_opts.delete_line },
        { "style.delete_token",             ConfigVariableType::Color,  &cv_style_opts.delete_token },
        { "style.delete_line_number",       ConfigVariableType::Color,  &cv_style_opts.delete_line_number },
        { "style.insert_line",              ConfigVariableType::Color,  &cv_style_opts.insert_line },
        { "style.insert_token",             ConfigVariableType::Color,  &cv_style_opts.insert_token },
        { "style.insert_line_number",       ConfigVariableType::Color,  &cv_style_opts.insert_line_number },
        { "style.common_line",              ConfigVariableType::Color,  &cv_style_opts.common_line },
        { "style.empty_cell",               ConfigVariableType::Color,  &cv_style_opts.empty_cell },
        { "style.common_line_number",       ConfigVariableType::Color,  &cv_style_opts.common_line_number },
        { "style.frame",                    ConfigVariableType::Color,  &cv_style_opts.frame },
    };
    // clang-format on

    config_apply_options(config_file_table_value, options);

    // Set up escape code heper struct values
    const std::vector<std::tuple<TermStyle*, std::string*>> colors = {
        {&cv_style_opts.background, &cv_style_escape_codes.background},
        {&cv_style_opts.header, &cv_style_escape_codes.header},
        {&cv_style_opts.delete_line, &cv_style_escape_codes.delete_line},
        {&cv_style_opts.delete_token, &cv_style_escape_codes.delete_token},
        {&cv_style_opts.delete_line_number, &cv_style_escape_codes.delete_line_number},
        {&cv_style_opts.insert_line, &cv_style_escape_codes.insert_line},
        {&cv_style_opts.insert_token, &cv_style_escape_codes.insert_token},
        {&cv_style_opts.insert_line_number, &cv_style_escape_codes.insert_line_number},
        {&cv_style_opts.common_line, &cv_style_escape_codes.common_line},
        {&cv_style_opts.common_line_number, &cv_style_escape_codes.common_line_number},
        {&cv_style_opts.frame, &cv_style_escape_codes.frame},
        {&cv_style_opts.empty_cell, &cv_style_escape_codes.empty_cell},
    };

    for (const auto& [source_value, dest_string] : colors) {
        dest_string->assign(source_value->to_ansi());
    }

    // Pick the built-in syntax palette (light vs dark) from the theme's
    // background: a bright background gets the light palette. A theme that leaves
    // the background as the terminal default (no RGB we can read) falls back to
    // dark — matching a typical terminal and the historical behaviour. Only used
    // for groups the theme's [syntax] table below doesn't override.
    {
        auto luminance = [](const TermColor& c) -> std::optional<double> {
            // Only 24-bit colours carry real RGB in r/g/b. For 4-bit/8-bit the
            // fields hold SGR code / palette index numbers (e.g. white bg == code
            // 107), so feeding them to the RGB formula misdetects the theme; treat
            // them as "no readable RGB" and fall back to the dark palette.
            if (c.kind == TermColor::Kind::Color24bit) {
                return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
            }
            return std::nullopt;
        };
        auto l = luminance(cv_style_opts.common_line.bg);
        if (!l)
            l = luminance(cv_style_opts.background.bg);
        cv_view_opts.light_theme = l.has_value() && *l > 140.0;
    }

    // Theme-owned syntax palette: replace any previous overrides with this theme's
    // [syntax] table (group name -> "#rrggbb"). Groups the theme omits fall back
    // to the built-in palette chosen above. Shared by all frontends; a frontend
    // may layer user overrides on top by calling set_syntax_color_override after.
    auto apply_syntax_table = [](Value& tree) -> bool {
        auto syntax = tree.lookup_value_by_path("syntax");
        if (!syntax || !syntax->get().is_table())
            return false;
        bool applied = false;
        syntax->get().as_table().for_each([&](const std::string& key, Value& value) {
            if (!value.is_string())
                return;
            const auto group = highlight_group_from_name(key);
            const auto color = TermColor::parse_string(value.as_string());
            if (group && *group != HighlightGroup::None && color) {
                set_syntax_color_override(*group, HlRgb{color->r, color->g, color->b});
                applied = true;
            }
        });
        return applied;
    };

    clear_syntax_overrides();
    if (!apply_syntax_table(config_file_table_value)) {
        // The on-disk theme predates the [syntax] section (existing installs keep
        // their file; config_write_bundled_themes won't clobber it). Fall back to
        // the matching bundled theme's palette so old configs still get colours.
        for (const auto& [name, content] : diffy::config_bundled_themes()) {
            if (name != theme)
                continue;
            ParseResult pr;
            Value bundled;
            if (cfg_parse_value_tree(content, pr, bundled) && pr.is_ok())
                apply_syntax_table(bundled);
            break;
        }
    }

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        if (config_file_table_value["settings"].key_comments.empty()) {
            config_file_table_value["settings"].key_comments.push_back(config_doc_theme);
        }
        config_save(config_root, config_path, config_file_table_value);
    }
}

diffy::Algo
diffy::algo_from_string(std::string s) {
    if (s == "p" || s == "patience" || s == "default")
        return Algo::kPatience;
    else if (s == "mg" || s == "myers-greedy")
        return Algo::kMyersGreedy;
    else if (s == "ml" || s == "myers-linear")
        return Algo::kMyersLinear;
    return Algo::kInvalid;
}
