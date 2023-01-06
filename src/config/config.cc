#include "config.hpp"

#include <config/parser/config_parser.hpp>
#include <config/parser/config_parser_utils.hpp>
#include <output/side_by_side.hpp>
#include <processing/tokenizer.hpp>
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

void
diffy::config_apply(diffy::ColumnViewCharacters& sbs_char_opts,
                    diffy::ColumnViewSettings& sbs_view_opts,
                    diffy::ColumnViewTextStyle& sbs_style_opts) {
    const std::string config_root = fmt::format("{}/diffy", sago::getConfigHome());
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

    auto translate_color = [](Value::Table& stored_table) {
        std::string parsed_color;
        {
            // Translate color object to ansi code
            auto fg = stored_table["fg"].as_string();
            auto bg = stored_table["bg"].as_string();
            auto attr = stored_table["attr"];

            std::vector<std::string> attributes;
            for (auto& attr_node : attr.as_array()) {
                attributes.push_back(attr_node.as_string());
            }

            diffy::translate_color_name_to_16pal_escape_sequence(
                fg, bg, diffy::color_lookup_attributes(attributes), parsed_color);
        }
        return parsed_color;
    };

    for (const auto& [path, type, ptr] : options) {
        // Do we have a value for this option in the config we loaded?
        if (auto stored_value = cfg_lookup_value_by_path(path, config_file_table_value); stored_value) {
            // Yes. So we take the value and write it into our settings struct.
            switch (type) {
                case C::Bool: {
                    *((bool*) ptr) = stored_value->get().as_bool();
                } break;
                case C::String: {
                    *((std::string*) ptr) = stored_value->get().as_string();
                } break;
                case C::Color: {
                    std::string parsed_color = translate_color(stored_value->get().as_table());
                    auto* str_ptr = (std::string*) ptr;
                    str_ptr->assign(parsed_color);
                } break;
            }
        } else {
            // No such setting in the stored file, so we use the default.
            // parse value from the option struct as a table
            switch (type) {
                case C::Bool: {
                    Value v{Value::Bool{*(bool*) ptr}};
                    cfg_set_value_at(path, config_file_table_value, v);
                } break;
                case C::String: {
                    Value v{Value::String{*(std::string*) ptr}};
                    cfg_set_value_at(path, config_file_table_value, v);
                } break;
                case C::Color: {
                    std::string data = *(std::string*) ptr;

                    ParseResult parse_result;
                    Value parsed_value;
                    if (cfg_parse_value_tree(data, parse_result, parsed_value)) {
                        cfg_set_value_at(path, config_file_table_value, parsed_value);

                        // We need to replace the value stored in the settings struct as it's used
                        // by the display layer :-/
                        std::string parsed_color = translate_color(parsed_value.as_table());
                        auto* str_ptr = (std::string*) ptr;
                        str_ptr->assign(parsed_color);

                    } else {
                        assert(0 && "invalid input data; check source");
                    }
                }
            }
        }
    }

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        std::error_code ec;
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
