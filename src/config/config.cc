#include "config.hpp"

#include <output/column_view.hpp>
#include <processing/tokenizer.hpp>
#include <util/color.hpp>
#include <util/config_parser/config_parser.hpp>
#include <util/config_parser/config_parser_utils.hpp>

#include <fmt/format.h>
#include <sago/platform_folders.h>

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
# values are the palette names below, and hex RGB colors:
#   '#RGB' and '#RRGGBB'. I.e '#F00' or '#FF0000' for bright red.
#
# Available color names (16 color palette SGR colors):
#   black, red, green, yellow, blue, magenta, cyan, light_gray,
#   dark_gray, light_red, light_green, light_yellow, light_blue,
#   light_magenta, light_cyan, white
#
# Available attributes:
#   'bold', 'dim', 'italic', 'underline',
#   'blink', 'inverse', 'hidden', 'strikethrough'
#
)foo";

static std::string config_doc_general = R"foo(# General configuration for ´diffy´
# 
# Configure default options. These can be overriden with command-line arguments.
#
# To use another theme, update `theme` to point to another theme file; i.e:
#   theme = 'custom_theme'  # load custom_theme.conf
# 
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
    std::filesystem::create_directory(config_root);

    FILE* f = fopen(config_name.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s' for writing.\n", config_name.c_str());
        fprintf(stderr, "   errno (%d) = %s\n", errno, strerror(errno));
        return;
    }

    std::string serialized = cfg_serialize(config_value);
    fwrite(serialized.c_str(), serialized.size(), 1, f);
    fclose(f);
}

using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;

static void
config_apply_options(diffy::Value& config, const OptionVector& options) {
    for (const auto& [path, type, ptr] : options) {
        // Do we have a value for this option in the config we loaded?
        if (auto stored_value = config.lookup_value_by_path(path); stored_value) {
            // Yes. So we take the value and write it into our settings struct.
            switch (type) {
                case ConfigVariableType::Bool: {
                    *((bool*) ptr) = stored_value->get().as_bool();
                } break;
                case ConfigVariableType::Int: {
                    *((int64_t*) ptr) = (int64_t) stored_value->get().is_int();
                } break;
                case ConfigVariableType::String: {
                    *((std::string*) ptr) = stored_value->get().as_string();
                } break;
                case ConfigVariableType::Color: {
                    auto style = diffy::TermStyle::from_value(stored_value->get().as_table());
                    if (style) {
                        *((diffy::TermStyle*) ptr) = *style;
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
            fmt::print("error: {}\n\twhile parsing: {}\n", config_parse_result.error, config_path);
        } break;
        case ConfigLoadResult::DoesNotExist: {
            fmt::print("warning: could not find default config. creating file:\n\t{}\n", config_path);
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

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        config_file_table_value["general"].key_comments.push_back(config_doc_general);
        config_save(config_root, config_path, config_file_table_value);
    }
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
            fmt::print("error: {}\n\twhile parsing: {}\n", config_parse_result.error, config_path);
        } break;
        case ConfigLoadResult::DoesNotExist: {
            if (theme == "theme_default") {
                fmt::print("warning: could not find default theme, creating file:\n\t{}\n", config_path);
                flush_config_to_disk = true;
            }
        } break;
    };

    if (!config_file_table_value.lookup_value_by_path("settings")) {
        config_file_table_value["settings"] = {Value::Table{}};
    }

    // Update the color table
    {
        const std::vector<std::string> palette_color_names = {
            "black",      "red",           "green",      "yellow",    "blue",        "magenta",
            "cyan",       "light_gray",    "dark_gray",  "light_red", "light_green", "light_yellow",
            "light_blue", "light_magenta", "light_cyan", "white",
        };

        if (!config_file_table_value.lookup_value_by_path("color_map.red")) {
            config_file_table_value.set_value_at("color_map.red", {"red"});
        }

        auto& color_values = config_file_table_value.lookup_value_by_path("color_map")->get();

        for (const auto& color : palette_color_names) {
            if (color_values.contains(color)) {
                auto& v = color_values[color];
                auto term_color = TermColor::from_value(v);
                if (term_color) {
                    color_map_set(color, *term_color);
                }
            }
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
        { "style.header",                   ConfigVariableType::Color,  &cv_style_opts.header },
        { "style.delete_line",              ConfigVariableType::Color,  &cv_style_opts.delete_line },
        { "style.delete_token",             ConfigVariableType::Color,  &cv_style_opts.delete_token },
        { "style.delete_line_number",       ConfigVariableType::Color,  &cv_style_opts.delete_line_number },
        { "style.insert_line",              ConfigVariableType::Color,  &cv_style_opts.insert_line },
        { "style.insert_token",             ConfigVariableType::Color,  &cv_style_opts.insert_token },
        { "style.insert_line_number",       ConfigVariableType::Color,  &cv_style_opts.insert_line_number },
        { "style.common_line",              ConfigVariableType::Color,  &cv_style_opts.common_line },
        { "style.empty_line",               ConfigVariableType::Color,  &cv_style_opts.empty_line },
        { "style.common_line_number",       ConfigVariableType::Color,  &cv_style_opts.common_line_number },
        { "style.frame",                    ConfigVariableType::Color,  &cv_style_opts.frame },
    };
    // clang-format on

    config_apply_options(config_file_table_value, options);

    // Set up escape code heper struct values
    const std::vector<std::tuple<TermStyle*, std::string*>> colors = {
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
        {&cv_style_opts.empty_line, &cv_style_escape_codes.empty_line},
    };

    for (const auto& [source_value, dest_string] : colors) {
        dest_string->assign(source_value->to_ansi());
    }

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        config_file_table_value["settings"].key_comments.push_back(config_doc_theme);
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
