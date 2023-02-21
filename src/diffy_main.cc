#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "output/column_view.hpp"
#include "output/edit_dump.hpp"
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
#include <cstdlib>
#include <fstream>
#include <gsl/span>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// TODO: What is it on windows? nul?
const std::string PATH_NULL = "/dev/null";

namespace {
bool
is_file_consumable(const std::string& path) {
#ifdef DIFFY_PLATFORM_WINDOWS
    // TODO: We should at least check that it's a file.
    return true;
#else
    if (path == PATH_NULL) {
        return true;
    }
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
            *result = diffy::MyersGreedy<diffy::Line>(diff_input).compute();
        } break;
        case diffy::Algo::kMyersLinear: {
            *result = diffy::MyersLinear<diffy::Line>(diff_input).compute();
        } break;
        case diffy::Algo::kPatience: {
            *result = diffy::Patience<diffy::Line>(diff_input).compute();
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
main(int argc, char* argv[], char * environ[]) {
    diffy::ProgramOptions opts;

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
    -n, --new-file             custom name to give the old-file (right)

    -i, --ignore-line-endings  ignore changes to line endings
    -w, --ignore-whitespace    ignore all changes to whitespace

    --list-colors              list all available colors available in the configuration
    
Side by side options:
    -l, --line                 line based diff instead of word based diff
    -W [width]                 maximum width in each column
)"),
                                       argv[0], diffy::config_get_directory());

#ifdef DIFFY_DEBUG
        help += R"(
    -d                         debug output
    -t                         dump test case code
        )";
#endif

        help += "\n";

        help += "Config directory:\n    " + diffy::config_get_directory() + "\n\n";

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
        while ((c = getopt_long(in_argc, in_argv, "a:dhlsS:uU:W:o:n:iw", long_options, &option_index)) >= 0) {
            switch (c) {
                case 'v':
                    fmt::print("version: {}\n", DIFFY_VERSION);
                    exit(0);
#ifdef DIFFY_DEBUG
                case 'd':
                    opts.debug = true;
                    break;
#endif
                case 'h':
                    opts.help = true;
                    return true;
                case '1': {
                    auto cap = diffy::tty_get_capabilities();
                    if (cap & diffy::TermColorSupport_Ansi4bit) {
                        puts("Found support for 16 color palette");
                    }
                    if (cap & diffy::TermColorSupport_Ansi8bit) {
                        puts("Found support for 256 color palette");
                    }
                    if (cap & diffy::TermColorSupport_Ansi24bit) {
                        puts("Found support for true color");
                    }
                    if (cap & diffy::TermColorSupport_None) {
                        puts("Found nothing. You have a terrible terminal, or the detection code is bad.");
                    }
                    puts("");
                    diffy::color_dump();
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
                        opts.column_view = true;
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

        if (opts.unified && opts.column_view) {
            show_help("error: -s and -S[context], -u -U[context] are mutually exclusive");
            return false;
        } else if (!opts.unified && !opts.column_view) {
            opts.column_view = true;
        }

        int positional_count = argc - optind;

        if (positional_count != 2) {
            show_help("error: missing positional arguments");
            return false;
        }

        opts.left_file = argv[optind];
        opts.right_file = argv[optind + 1];

        auto git_prefix = getenv("GIT_PREFIX");
        auto git_base = getenv("BASE");
        if (git_prefix != nullptr && git_base != nullptr) {
            opts.left_file_name = git_base;
        } else if (opts.left_file_name.empty()) {
            opts.left_file_name = opts.left_file;
        }

        if (opts.right_file_name.empty()) {
            opts.right_file_name = opts.right_file;
        }

        // TODO: Check if file is readable.
        bool a_valid = is_file_consumable(opts.left_file);
        bool b_valid = is_file_consumable(opts.right_file);

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

    // Load the global defaults before we override them with command line args
    diffy::config_apply_options(opts);

    diffy::ColumnViewState cv_ui_opts;
    diffy::config_apply_theme(opts.theme, cv_ui_opts.chars, cv_ui_opts.settings, cv_ui_opts.style_config,
                              cv_ui_opts.style);

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

    if (opts.debug) {
        fmt::print("input (N/M: {}/{})\n", diff_input.A.size(), diff_input.B.size());
        fmt::print("edit_sequence (size: {})\n", result.edit_sequence.size());
        diffy::edit_dump_diff_render(diff_input, result);
    } else if (opts.column_view) {
        const auto& annotated_hunks = annotate_hunks(
            diff_input, hunks,
            opts.line_granularity ? diffy::EditGranularity::Line : diffy::EditGranularity::Token,
            opts.ignore_whitespace);
        diffy::column_view_diff_render(diff_input, annotated_hunks, cv_ui_opts, opts.width);
    } else if (opts.unified) {
        auto unified_lines = diffy::unified_diff_render(diff_input, hunks);
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
