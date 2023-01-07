#pragma once

#include "parser/config_parser.hpp"

#include <string>

namespace diffy {

enum class Algo { kInvalid, kMyersGreedy, kMyersLinear, kPatience };

Algo
algo_from_string(std::string s);

struct ProgramOptions {
    bool debug = false;
    bool help = false;
    bool column_view = false;
    bool line_granularity = false;
    bool unified = false;
    bool test_case = false;
    Algo algorithm = Algo::kPatience;
    int64_t context_lines = 3;
    int64_t width = 0;

    bool ignore_line_endings = false;
    bool ignore_whitespace = false;

    std::string left_file;
    std::string right_file;

    std::string left_file_name;
    std::string right_file_name;
};

// TODO: Hmm... but this should be a Color type?
// Color.to_value()
struct ColumnViewTextStyle {
    // clang-format off
    Value header = { Value::Table {
        { "fg", Value {"white"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {
            {"underline"}
        }},
    }}};

    Value delete_line = { Value::Table {
        { "fg", Value {"default"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {}},
    }}};

    Value delete_token = { Value::Table {
        { "fg", Value {"light_red"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {
            {"bold"}
        }},
    }}};

    Value delete_line_number = { Value::Table {
        { "fg", Value {"red"} },
        { "bg", Value {"dark_red"} },
        { "attr", { Value::Array {
            {"bold"}
        }},
    }}};

    Value insert_line = { Value::Table {
        { "fg", Value {"default"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {}},
    }}};

    Value insert_token = { Value::Table {
        { "fg", Value {"light_green"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {
            {"bold"}
        }},
    }}};

    Value insert_line_number = { Value::Table {
        { "fg", Value {"green"} },
        { "bg", Value {"dark_green"} },
        { "attr", { Value::Array {
            {"bold"}
        }},
    }}};

    Value common_line = { Value::Table {
        { "fg", Value {"default"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {}},
    }}};

    Value common_line_number = { Value::Table {
        { "fg", Value {"default"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {}},
    }}};

    Value frame = { Value::Table {
        { "fg", Value {"dark_grey"} },
        { "bg", Value {"light_grey"} },
        { "attr", { Value::Array {}},
    }}};

    Value empty_line = { Value::Table {
        { "fg", Value {"white"} },
        { "bg", Value {"default"} },
        { "attr", { Value::Array {}},
    }}};
    // clang-format on
};

struct ColumnViewTextStyleEscapeCodes {
    std::string header;
    std::string delete_line;
    std::string delete_token;
    std::string delete_line_number;
    std::string insert_line;
    std::string insert_token;
    std::string insert_line_number;
    std::string common_line;
    std::string common_line_number;
    std::string frame;
    std::string empty_line;
};

struct ColumnViewCharacters {
    std::string column_separator = " │";
    std::string edge_separator = "";

    std::string tab_replacement = "→   ";
    std::string cr_replacement = "←";  // ␍
    std::string lf_replacement = "↓";
    std::string crlf_replacement = "↵";  // ␤
    std::string space_replacement = "·";
};

struct ColumnViewSettings {
    bool show_line_numbers = true;
    bool context_colored_line_numbers = true;
    bool word_wrap = true;
    bool line_number_align_right = false;
};

void
config_apply(diffy::ColumnViewCharacters& sbs_char_opts,
             diffy::ColumnViewSettings& sbs_view_opts,
             diffy::ColumnViewTextStyle& sbs_style_opts,
             diffy::ColumnViewTextStyleEscapeCodes& sbs_style_escape_codes);

}  // namespace diffy