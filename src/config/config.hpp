#pragma once

#include <string>

namespace diffy {

enum class Algo { kInvalid, kMyersGreedy, kMyersLinear, kPatience };

Algo
algo_from_string(std::string s);

struct ProgramOptions {
    bool debug = false;
    bool help = false;
    bool side_by_side = false;
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

struct ColumnViewConfig {
    std::string column_separator = " │";
    std::string edge_separator = "";

    std::string tab_replacement = "→   ";
    std::string cr_replacement = "←";  // ␍
    std::string lf_replacement = "↓";
    std::string crlf_replacement = "↵";  // ␤
    std::string space_replacement = "·";

    bool show_line_numbers = true;
    bool context_colored_line_numbers = true;
    bool word_wrap = true;
    bool line_number_align_right = false;

    // clang-format off
    std::string theme_header             = "{fg='white', bg='default', attr=['underline']}";
    std::string theme_delete_line        = "{fg='default', bg='default', attr=[]}";
    std::string theme_delete_token       = "{fg='light_red', bg='default', attr=['bold']}";
    std::string theme_delete_line_number = "{fg='red', bg='dark_red', attr=[]}";
    std::string theme_insert_line        = "{fg='default', bg='default', attr=[]}";
    std::string theme_insert_token       = "{fg='light_green', bg='default', attr=['bold']}";
    std::string theme_insert_line_number = "{fg='green', bg='darg_green', attr=['bold']}";
    std::string theme_common_line        = "{fg='default', bg='default', attr=[]}";
    std::string theme_common_line_number = "{fg='default', bg='default', attr=[]}";
    std::string theme_frame              = "{fg='dark_grey', bg='light_grey', attr=[]}";
    std::string theme_empty_line         = "{fg='white', bg='default', attr=[]}";
    // clang-format on

    // -- @cleanup below, state not configuration

    // automatically calculated based on terminal width?
    int64_t max_row_length = 0;
    // This is automatically adjusted depending on how many lines we show.
    int64_t line_number_digits_count = 4;
};

void
config_apply(diffy::ColumnViewConfig& sbs_ui_opts);

}  // namespace diffy