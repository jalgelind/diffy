#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "output/edit_dump.hpp"
#include "output/side_by_side.hpp"
#include "output/unified.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "processing/tokenizer.hpp"
#include "util/color.hpp"
#include "util/hash.hpp"
#include "util/readlines.hpp"
#include "util/tty.hpp"

#include <musl/getopt.h>
#include <sys/stat.h>

#include <fmt/format.h>

#include <cstdio>
#include <fstream>
#include <gsl/span>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
bool
is_file_or_pipe_or_link(const std::string& path) {
#ifdef DIFFY_PLATFORM_WINDOWS
    // TODO: We should at least check that it's a file.
    return true;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == -1) {
        return false;
    }
    return S_ISREG(st.st_mode) || S_ISFIFO(st.st_mode) || S_ISLNK(st.st_mode);
#endif
}

bool
compute_diff(diffy::Algo algorithm,
             bool ignore_whitespace,
             diffy::DiffInput<diffy::Line> diff_input,
             diffy::DiffResult* result) {
    switch (algorithm) {
        case diffy::Algo::kMyersGreedy: {
            diffy::MyersGreedy<diffy::Line> mgdiff(diff_input);
            *result = mgdiff.compute();
        } break;
        case diffy::Algo::kMyersLinear: {
            diffy::MyersLinear<diffy::Line> mldiff(diff_input);
            *result = mldiff.compute();
        } break;
        case diffy::Algo::kPatience: {
            diffy::Patience<diffy::Line> pdiff(diff_input);
            *result = pdiff.compute();
        } break;
        case diffy::Algo::kInvalid:
            /* fall-through */
        default: {
            puts("Invalid algorithm");
            return false;
        } break;
    }

    // Ignore change if both sides are various forms of empty
    if (ignore_whitespace) {
        for (auto& seq : result->edit_sequence) {
            if ((int) seq.a_index >= (int) diff_input.A.size() ||
                (int) seq.b_index >= (int) diff_input.B.size()) {
                continue;
            }
            if (diffy::is_empty(diff_input.A[seq.a_index].line) &&
                diffy::is_empty(diff_input.B[seq.b_index].line)) {
                seq.type = diffy::EditType::Common;
            }
        }
    }

    return true;
}

}  // namespace

int
main(int argc, char* argv[]) {
    diffy::ProgramOptions opts;
    diffy::ColumnViewState sbs_ui_opts;

    diffy::config_apply(sbs_ui_opts.chars, sbs_ui_opts.settings, sbs_ui_opts.style);

    auto show_help = [&](const std::string& optional_error_message) {
        std::string help = fmt::format((R"(
Usage: {} [options] left_file right_file

Compare files line by line, side by side

Options:
    -v, --version              show program version and exit
    -a --algorithm [algorithm] which algorithm to use for line diffing
                                    myers-linear (ml)
                                    myers-greedy (mg)
                                    patience     (p)
    -u, -U [context_lines]     show unified output, optional context line count
    -s, -S [context_lines]     show side-by-side column output, optional context line count

    -o, --old-file             custom name to give the old-file (left)
    -n, --new-file             custom name to give the old-file (left)

    -i, --ignore-line-endings  ignore changes to line endings
    -w, --ignore-whitespace    ignore all changes to whitespace

    --list-colors              list all available colors available in the configuration
    
Side by side options:
    -l, --line                 line based diff instead of word based diff
    -W [width]                 maximum width in each column
)"),
                                       argv[0]);

#ifdef DIFFY_DEBUG
        help += R"(
    -d                         debug output
    -t                         dump test case code
        )";
#endif

        help += "\n";

        if (!optional_error_message.empty()) {
            help += optional_error_message;
        }
        puts(help.c_str());
    };

    auto parse_args = [&](int in_argc, char* in_argv[]) {
        static struct option long_options[] = {{"debug", no_argument, 0, 'd'},
                                               {"help", no_argument, 0, 'h'},
                                               {"side-by-side", optional_argument, 0, 'S'},
                                               {"line", optional_argument, 0, 'l'},
                                               {"unified", optional_argument, 0, 'U'},
                                               {"version", no_argument, 0, 'v'},
                                               {"width", optional_argument, 0, 'W'},
                                               {"algorithm", optional_argument, 0, 'a'},
                                               {"old-file", optional_argument, 0, 'o'},
                                               {"new-file", optional_argument, 0, 'n'},
                                               {"ignore-line-endings", no_argument, 0, 'i'},
                                               {"ignore-whitespace", no_argument, 0, 'w'},
                                               {"list-colors", no_argument, 0, '1'},
                                               {0, 0, 0, 0}};
        int c = 0, option_index = 0;
        while ((c = getopt_long(in_argc, in_argv, "a:dhlsS:tuU:W:o:n:iw", long_options, &option_index)) >=
               0) {
            switch (c) {
                case 'v':
                    fmt::print("version: {}\n", DIFFY_VERSION);
                    exit(0);
#ifdef DIFFY_DEBUG
                case 'd':
                    opts.debug = true;
                    break;
                case 't':
                    opts.test_case = true;
                    break;
#endif
                case 'h':
                    opts.help = true;
                    return true;
                case '1': {
                    puts("Checking color capabilities...");
                    puts("");
                    switch (diffy::get_terminal_color_capability()) {
                        case diffy::TerminalColorCapability::Ansi4bit: {
                            puts("\tYour terminal supports a 16 colors palette");
                        } break;
                        case diffy::TerminalColorCapability::Ansi8bit: {
                            puts("\tYour terminal supports a 256 color palette");
                        } break;
                        case diffy::TerminalColorCapability::Ansi24bit: {
                            puts("\tYour terminal supports true color");
                        } break;
                        case diffy::TerminalColorCapability::None: {
                            puts("\tYour have a terrible terminal.");
                        } break;
                    }
                    puts("");
                    diffy::dump_colors();
                    exit(0);
                }
                case 'a':
                    opts.algorithm = diffy::algo_from_string(optarg);
                    break;
                case 'l':
                    opts.line_granularity = true;
                    break;
                case 'o':
                    opts.left_file_name = optarg;
                    break;
                case 'n':
                    opts.right_file_name = optarg;
                    break;
                case 'i':
                    opts.ignore_line_endings = true;
                    break;
                case 'w':
                    opts.ignore_whitespace = true;
                    break;
                case 'u':
                case 's':
                case 'U':
                case 'S': {
                    if (c == 'S' || c == 's') {
                        opts.side_by_side = true;
                    } else if (c == 'U' || c == 'u') {
                        opts.unified = true;
                    }

                    if (optarg) {
                        // @cleanup
                        if (isdigit(optarg[0])) {
                            opts.context_lines = atoi(optarg);
                        } else {
                            show_help(fmt::format("error: invalid value for -{} ({})\n", static_cast<char>(c),
                                                  optarg));
                            return false;
                        }
                    }
                    break;
                }
                case 'W': {
                    if (optarg) {
                        // @cleanup
                        if (isdigit(optarg[0])) {
                            opts.width = atoi(optarg);
                        } else {
                            show_help(fmt::format("error: invalid width value for -{} ({})\n",
                                                  static_cast<char>(c), optarg));
                            return false;
                        }
                    }
                    break;
                }
                case '?':
                    show_help("error: invalid option");
                    return false;
                default:
                    show_help(fmt::format("error: invalid option: -{}", static_cast<char>(c)));
                    return false;
            }
        }

        if (opts.unified && opts.side_by_side) {
            show_help("error: -s -S[context] -s -S[context] are mutually exclusive");
            return false;
        } else if (!opts.unified && !opts.side_by_side) {
            opts.side_by_side = true;
        }

        int positional_count = argc - optind;

        if (positional_count != 2) {
            show_help("error: missing positional arguments");
            return false;
        }

        opts.left_file = argv[optind];
        opts.right_file = argv[optind + 1];

        if (opts.left_file_name.empty()) {
            opts.left_file_name = opts.left_file;
        }

        if (opts.right_file_name.empty()) {
            opts.right_file_name = opts.right_file;
        }

        // TODO: Check if file is readable.
        bool a_valid = is_file_or_pipe_or_link(opts.left_file);
        bool b_valid = is_file_or_pipe_or_link(opts.right_file);

        if (!a_valid || !b_valid) {
            std::string err;
            if (!a_valid)
                err += fmt::format("{}: No such file\n", opts.left_file);
            if (!b_valid)
                err += fmt::format("{}: No such file\n", opts.right_file);
            show_help(err);
            return false;
        }

        return true;
    };

    if (!parse_args(argc, argv)) {
        return -1;
    }

    if (opts.help) {
        show_help("");
        return 0;
    }

    auto left_line_data = diffy::readlines(opts.left_file, opts.ignore_line_endings);
    auto right_line_data = diffy::readlines(opts.right_file, opts.ignore_line_endings);

    gsl::span<diffy::Line> left_lines{left_line_data};
    gsl::span<diffy::Line> right_lines{right_line_data};

    diffy::DiffInput<diffy::Line> diff_input{left_lines, right_lines, opts.left_file_name,
                                             opts.right_file_name};

    diffy::DiffResult result;
    if (!compute_diff(opts.algorithm, opts.ignore_whitespace, diff_input, &result)) {
        return -1;
    }

    if (result.status == diffy::DiffResultStatus::NoChanges) {
        // In order to be compatible with 'diff', don't output this
        // helpful message in unified output mode.
        if (!opts.unified)
            puts("No changes.");
        return 0;
    } else if (result.status != diffy::DiffResultStatus::OK) {
        puts("Diff compute failed");
        return 1;
    }

    auto hunks = diffy::compose_hunks(result.edit_sequence, opts.context_lines);

    if (opts.test_case) {
        // TODO: Finish this; generate unit tests from functional tests
        auto EditType_to_string = [](diffy::EditType t) {
            std::string s[] = {"Delete", "Insert", "Common", "Meta"};
            return fmt::format("EditType::{}", s[(int) t]);
        };
        fmt::print("dump test case\n");
        for (auto edit : result.edit_sequence) {
            fmt::print("{{");
            fmt::print("{}", EditType_to_string(edit.type));
            fmt::print("}}");
        }
    } else if (opts.debug) {
        // TODO: Dump hunks?
        (void) hunks;
        fmt::print("input (N/M: {}/{})\n", diff_input.A.size(), diff_input.B.size());
        fmt::print("edit_sequence (size: {})\n", result.edit_sequence.size());
        diffy::dump_diff_edits(diff_input, result);
    } else if (opts.side_by_side) {
        const auto& annotated_hunks = annotate_hunks(
            diff_input, hunks,
            opts.line_granularity ? diffy::EditGranularity::Line : diffy::EditGranularity::Token,
            opts.ignore_whitespace);
        diffy::side_by_side_diff(diff_input, annotated_hunks, sbs_ui_opts, opts.width);
    } else if (opts.unified) {
        auto unified_lines = diffy::get_unified_diff(diff_input, hunks);
        for (const auto& line : unified_lines) {
            if (line[line.size() - 1] == '\n')
                printf("%s", line.c_str());
            else {
                printf("%s\n", line.c_str());
                printf("\\ No newline at end of file\n");
            }
        }
        return 1;
    }

    return 0;
}
