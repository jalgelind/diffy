#pragma once

#include "util/color.hpp"
#include "util/config_parser/config_parser.hpp"

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
    Algo algorithm = Algo::kPatience;
    int64_t context_lines = 3;
    int64_t width = 0;

    std::string theme = "theme_default";

    bool ignore_line_endings = true;
    bool ignore_whitespace = true;

    std::string left_file;
    std::string right_file;

    std::string left_file_name;
    std::string right_file_name;
};

struct ColumnViewTextStyle {
    // clang-format off
    TermStyle header = TermStyle {
        TermColor::kWhite,
        TermColor::kNone,
        TermStyle::Attribute::Underline
    };

    TermStyle header_background = TermStyle {
        TermColor::kWhite,
        TermColor::kNone,
        TermStyle::Attribute::Underline
    };

    TermStyle delete_line = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };

    TermStyle delete_token = TermStyle {
        TermColor::kRed,
        TermColor::kNone,
        TermStyle::Attribute::Bold
    };

    TermStyle delete_line_number = TermStyle {
        TermColor::kRed,
        TermColor::kNone,
        TermStyle::Attribute::Bold
    };

    TermStyle insert_line = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };

    TermStyle insert_token = TermStyle {
        TermColor::kGreen,
        TermColor::kNone,
        TermStyle::Attribute::Bold
    };

    TermStyle insert_line_number = TermStyle {
        TermColor::kGreen,
        TermColor::kNone,
        TermStyle::Attribute::Bold
    };

    TermStyle common_line = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };

    TermStyle common_line_number = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };

    TermStyle frame = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
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
config_apply_options(diffy::ProgramOptions& program_options);

void
config_apply_theme(const std::string& theme,
                   diffy::ColumnViewCharacters& cv_char_opts,
                   diffy::ColumnViewSettings& cv_view_opts,
                   diffy::ColumnViewTextStyle& cv_style_opts,
                   diffy::ColumnViewTextStyleEscapeCodes& cv_style_escape_codes);

}  // namespace diffy
