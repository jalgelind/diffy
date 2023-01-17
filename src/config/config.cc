#include "config.hpp"

#include <output/column_view.hpp>
#include <processing/tokenizer.hpp>
#include <util/config_parser/config_parser.hpp>
#include <util/config_parser/config_parser_utils.hpp>
#include <util/color.hpp>

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

enum class ConfigVariableType {
    Bool,
    String,
    Color,
};

std::string
diffy::config_get_directory() {
    return fmt::format("{}/diffy", sago::getConfigHome());
}

void
diffy::config_apply(diffy::ProgramOptions& program_options,
                    diffy::ColumnViewCharacters& sbs_char_opts,
                    diffy::ColumnViewSettings& sbs_view_opts,
                    diffy::ColumnViewTextStyle& sbs_style_opts,
                    diffy::ColumnViewTextStyleEscapeCodes& sbs_style_escape_codes) {
    const std::string config_root = config_get_directory();
    const std::string config_path = fmt::format("{}/config.conf", config_root);
    bool flush_config_to_disk = false;

    Value config_file_table_value{Value::Table{}};
    {
        ParseResult load_result;

        if (cfg_load_file(config_path, load_result, config_file_table_value)) {
            assert(config_file_table_value.is_table() && "Expected a table");
            // Below we are going to assume we can access the value via as_table().
        } else {
            if (load_result.kind == diffy::ParseErrorKind::File) {
                flush_config_to_disk = true;
                fmt::print("warning: could not find default config. creating one.\n\t{}\n", config_path);
            } else {
                fmt::print("error: {}\n", load_result.error);
            }
        }
    }

#if DIFFY_DEBUG
    // TODO: Why don't we return ParseResult and add a bool operation on it?
    diffy::ParseResult result;

    // TODO: Why is test1 causing the serializer to barf the empty key?
    diffy::Value obj;
    if (cfg_parse_value_tree("[section]", result, obj)) {
        diffy::Value tmp1 { diffy::Value::Table {} };
        tmp1.set_value_at("colors", { Value::Table {} });
        fmt::print("TEST: {}\n", cfg_serialize(obj));
        fmt::print("TEST: {}\n", cfg_serialize(tmp1));
    }
#endif

    // Update the color table
    {
        const std::vector<std::string> palette_color_names = {
            "black",
            "red",
            "green",
            "yellow",
            "blue",
            "magenta",
            "cyan",
            "light_gray",
            "dark_gray",
            "light_red",
            "light_green",
            "light_yellow",
            "light_blue",
            "light_magenta",
            "light_cyan",
            "white",
        };

        if (!config_file_table_value.contains("colors")) {
            // TODO: We have to include something here to avoid broken output
            // (empty section had equal sign followed by empty table)
            config_file_table_value.set_value_at("colors.example", { "#f1f1f1" });

        }

        auto& color_values = config_file_table_value["colors"];

        for (const auto& color: palette_color_names) {
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
    using C = ConfigVariableType;
    // clang-format off
    const std::vector<std::tuple<std::string, ConfigVariableType, void*>> options = {
        // side-by-side settings
        { "general.show_line_numbers",            C::Bool, &sbs_view_opts.show_line_numbers},
        { "general.context_colored_line_numbers", C::Bool, &sbs_view_opts.context_colored_line_numbers},
        { "general.word_wrap",                    C::Bool, &sbs_view_opts.word_wrap},
        { "general.line_number_align_right",      C::Bool, &sbs_view_opts.line_number_align_right},

        // side-by-side theme
        { "theme.column_separator",         C::String, &sbs_char_opts.column_separator },
        { "theme.edge_separator",           C::String, &sbs_char_opts.edge_separator },
        { "theme.tab_replacement",          C::String, &sbs_char_opts.tab_replacement },
        { "theme.cr_replacement",           C::String, &sbs_char_opts.cr_replacement },
        { "theme.lf_replacement",           C::String, &sbs_char_opts.lf_replacement },
        { "theme.crlf_replacement",         C::String, &sbs_char_opts.crlf_replacement },
        { "theme.space_replacement",        C::String, &sbs_char_opts.space_replacement },

        // side-by-side color style
        { "theme.header",                   C::Color,  &sbs_style_opts.header },
        { "theme.delete_line",              C::Color,  &sbs_style_opts.delete_line },
        { "theme.delete_token",             C::Color,  &sbs_style_opts.delete_token },
        { "theme.delete_line_number",       C::Color,  &sbs_style_opts.delete_line_number },
        { "theme.insert_line",              C::Color,  &sbs_style_opts.insert_line },
        { "theme.insert_token",             C::Color,  &sbs_style_opts.insert_token },
        { "theme.insert_line_number",       C::Color,  &sbs_style_opts.insert_line_number },
        { "theme.common_line",              C::Color,  &sbs_style_opts.common_line },
        { "theme.empty_line",               C::Color,  &sbs_style_opts.empty_line },
        { "theme.common_line_number",       C::Color,  &sbs_style_opts.common_line_number },
        { "theme.frame",                    C::Color,  &sbs_style_opts.frame },
    };
    // clang-format on

    for (const auto& [path, type, ptr] : options) {
        // Do we have a value for this option in the config we loaded?
        if (auto stored_value = config_file_table_value.lookup_value_by_path(path); stored_value) {
            // Yes. So we take the value and write it into our settings struct.
            switch (type) {
                case C::Bool: {
                    *((bool*) ptr) = stored_value->get().as_bool();
                } break;
                case C::String: {
                    *((std::string*) ptr) = stored_value->get().as_string();
                } break;
                case C::Color: {
                    auto style = TermStyle::from_value(stored_value->get().as_table());
                    if (style) {
                        *((TermStyle*) ptr) = *style;
                    }
                } break;
            }
        } else {
            // No such setting in the stored file, so we store the default value
            // from the struct.
            switch (type) {
                case C::Bool: {
                    Value v{Value::Bool{*(bool*) ptr}};
                    config_file_table_value.set_value_at(path, v);
                } break;
                case C::String: {
                    Value v{Value::String{*(std::string*) ptr}};
                    config_file_table_value.set_value_at(path, v);
                } break;
                case C::Color: {
                    TermStyle* style = (TermStyle*) ptr;
                    config_file_table_value.set_value_at(path, style->to_value());
                }
            }
        }
    }

    // Set up escape code heper struct values
    const std::vector<std::tuple<TermStyle*, std::string*>> colors = {
            {&sbs_style_opts.header, &sbs_style_escape_codes.header},
            {&sbs_style_opts.delete_line, &sbs_style_escape_codes.delete_line},
            {&sbs_style_opts.delete_token, &sbs_style_escape_codes.delete_token},
            {&sbs_style_opts.delete_line_number, &sbs_style_escape_codes.delete_line_number},
            {&sbs_style_opts.insert_line, &sbs_style_escape_codes.insert_line},
            {&sbs_style_opts.insert_token, &sbs_style_escape_codes.insert_token},
            {&sbs_style_opts.insert_line_number, &sbs_style_escape_codes.insert_line_number},
            {&sbs_style_opts.common_line, &sbs_style_escape_codes.common_line},
            {&sbs_style_opts.common_line_number, &sbs_style_escape_codes.common_line_number},
            {&sbs_style_opts.frame, &sbs_style_escape_codes.frame},
            {&sbs_style_opts.empty_line, &sbs_style_escape_codes.empty_line},
    };

    for (const auto &[source_value, dest_string] : colors) {
        dest_string->assign(source_value->to_ansi());
    }

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {

        // Add help text to the configuration file
        config_file_table_value["colors"].key_comments.push_back(
R"foo( Color palette customization
# Available color names:
#   black, red, green, yellow, blue, magenta, cyan, light_gray,
#   dark_gray, light_red, light_green, light_yellow, light_blue,
#   light_magenta, light_cyan, white,
#
# Example:
#   [colors]
#       white = "#f0f0f0"
)foo");

        config_file_table_value["theme"].key_comments.push_back(
R"foo( Theme configuration
# 
# TODO: fill this in with whatever we come up with
# TODO: and since you're seeing this maybe you should fix the
#       serializer formatting to avoid the exessive newlining?
#       maybe align the values nicely?
# TODO: also look at the extra space at the start of this string
#       ...
# 
)foo");

        std::error_code ec; // TODO: use
        std::filesystem::create_directory(config_root, ec);

        do {
            FILE* f = fopen(config_path.c_str(), "wb");
            if (!f) {
                fprintf(stderr, "Failed to open '%s' for writing.\n", config_path.c_str());
                fprintf(stderr, "   errno (%d) = %s\n", errno, strerror(errno));
                break;
            }

            std::string serialized = cfg_serialize(config_file_table_value);
            fwrite(serialized.c_str(), serialized.size(), 1, f);
            fclose(f);
        } while (0);
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
