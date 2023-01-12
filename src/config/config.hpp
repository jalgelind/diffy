#pragma once

#include "parser/config_parser.hpp"
#include "util/color.hpp"

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

struct ColumnViewTextStyle {
    // clang-format off
    TermStyle header = TermStyle {
        TermColor::kWhite,
        TermColor::kDefault,
        TermStyle::Attribute::Underline
    };

    // TODO: can we make this more flexible; adding color options requires making changes in a lot of places? and the mirror struct...
    // maybe we can use a hashmap instead.

    // TODO: maybe TermStyle can have an optional char in it?
    //       will that cover all cases ui chars used on screen?
    //       would be nice to remove ColumnViewCharacters
    //       character repeat flag? string edges flag? something to use multiple symbols as separators, header wrappers adding e.g a fade

    // flag: conter contents? where does width come from? we might have it stored somewhere since it's a fixed size
    // std::optional<std::string> prefix_chars;
    // std::optional<std::string> postfix_chars;
    // TermStyle::to_ansi_with_decorations(prefix_chars, postfix_chars, text)

    TermStyle header_background = TermStyle {
        TermColor::kWhite,
        TermColor::kDefault,
        TermStyle::Attribute::Underline
    };

    TermStyle delete_line = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle delete_token = TermStyle {
        TermColor::kBlack,
        TermColor::kLightRed,
        TermStyle::Attribute::Bold
    };

    TermStyle delete_line_number = TermStyle {
        TermColor::kRed,
        TermColor::kBlack,
        TermStyle::Attribute::Bold
    };

    TermStyle insert_line = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle insert_token = TermStyle {
        TermColor::kBlack,
        TermColor::kGreen,
        TermStyle::Attribute::Bold
    };

    TermStyle insert_line_number = TermStyle {
        TermColor::kRed,
        TermColor::kBlack,
        TermStyle::Attribute::Bold
    };

    TermStyle common_line = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle common_line_number = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle frame = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle empty_line = TermStyle {
        TermColor::kWhite,
        TermColor::kLightGray,
        TermStyle::Attribute::None
    };
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

std::string
config_get_directory();

void
config_apply(diffy::ColumnViewCharacters& sbs_char_opts,
             diffy::ColumnViewSettings& sbs_view_opts,
             diffy::ColumnViewTextStyle& sbs_style_opts,
             diffy::ColumnViewTextStyleEscapeCodes& sbs_style_escape_codes);

}  // namespace diffy