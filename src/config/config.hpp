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
        TermColor::kBlue,
        TermColor::kDefault,
        TermStyle::Attribute::Underline
    };

    TermStyle delete_line = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle delete_token = TermStyle {
        TermColor::kLightRed,
        TermColor::kBlack,
        TermStyle::Attribute::Bold
    };

    TermStyle delete_line_number = TermStyle {
        TermColor::kRed,
        TermColor::kDefault,
        TermStyle::Attribute::Bold
    };

    TermStyle insert_line = TermStyle {
        TermColor::kDefault,
        TermColor::kDefault,
        TermStyle::Attribute::None
    };

    TermStyle insert_token = TermStyle {
        TermColor::kLightGreen,
        TermColor::kBlack,
        TermStyle::Attribute::Bold
    };

    TermStyle insert_line_number = TermStyle {
        TermColor::kDefault,
        TermColor::kGreen,
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
        TermColor::kDarkGray,
        TermColor::kLightGray,
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

void
config_apply(diffy::ColumnViewCharacters& sbs_char_opts,
             diffy::ColumnViewSettings& sbs_view_opts,
             diffy::ColumnViewTextStyle& sbs_style_opts,
             diffy::ColumnViewTextStyleEscapeCodes& sbs_style_escape_codes);

}  // namespace diffy