#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "output/column_view.hpp"
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
#include <filesystem>
#include <fstream>
#include <gsl/span>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace diffy {

enum class FileStatus {
    kOk,
    kNullPath,
    kFileDoesNotExist,
    kFileNotReadable,
    kNoPermission,
};

FileStatus
check_file_status(const std::string& path) {
    if (path.empty() || path == "/dev/null" || path == "nul") {
        return FileStatus::kNullPath;
    }

    fs::path file_path(path);

    if (!fs::exists(file_path)) {
        return FileStatus::kFileDoesNotExist;
    }

    if (!(fs::is_regular_file(file_path) || fs::is_fifo(file_path) || fs::is_symlink(file_path))) {
        return FileStatus::kFileNotReadable;
    }

    auto perms = fs::status(path).permissions();
    if (((perms & fs::perms::owner_read) == fs::perms::none) &&
        ((perms & fs::perms::group_read) == fs::perms::none)) {
        return FileStatus::kNoPermission;
    }

    return FileStatus::kOk;
}

std::string
to_string(const FileStatus error_code) {
    switch (error_code) {
        case FileStatus::kOk:
            return "Success";
        case FileStatus::kFileDoesNotExist:
            return "File does not exist";
        case FileStatus::kFileNotReadable:
            return "File is not readable (invalid file)";
        case FileStatus::kNoPermission:
            return "File is not readable (no permission)";
        case FileStatus::kNullPath:
            return "Null path";
        default:
            return "Unknown error";
    }
}

std::optional<std::filesystem::perms>
read_file_permissions(const std::string& path) {
    if (check_file_status(path) == FileStatus::kFileDoesNotExist) {
        return std::nullopt;
    }
    fs::directory_entry file(path);
    std::string perms;
    return file.status().permissions();
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
    bool invoked_as_git_tool = getenv("GIT_PREFIX") != nullptr;

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
    -u, -U [context_lines]       show unified output, optional context line count
    -s, -S [context_lines]       show side-by-side column output, optional context line count

    -o, --old-file               custom name to give the old-file (left)
    -n, --new-file               custom name to give the old-file (right)

    -i, --ignore-line-endings    ignore changes to line endings
    -I, --no-ignore-line-endings inverse of --ignore-line-endings

    -w, --ignore-whitespace      ignore all changes to whitespace
    -W, --no-ignore-whitespace   inverse of --ignore-whitespace

    --list-colors                list all available colors available in the configuration
    
Side by side options:
    -l, --line                   line based diff instead of word based diff
    -W [width]                   maximum width in each column
)"),
                                       argv[0], diffy::config_get_directory());

        help += "\n";

        help += "Config directory:\n    " + diffy::config_get_directory() + "\n\n";

        if (!optional_error_message.empty()) {
            help += optional_error_message;
        }
        puts(help.c_str());
    };

    auto parse_args = [&](int in_argc, char* in_argv[]) {
        static struct option long_options[] = {{"help", no_argument, 0, 'h'},
                                               {"side-by-side", optional_argument, 0, 'S'},
                                               {"line", optional_argument, 0, 'l'},
                                               {"unified", optional_argument, 0, 'U'},
                                               {"version", no_argument, 0, 'v'},
                                               {"width", optional_argument, 0, 'W'},
                                               {"algorithm", optional_argument, 0, 'a'},
                                               {"old-file", optional_argument, 0, 'o'},
                                               {"new-file", optional_argument, 0, 'n'},
                                               {"ignore-line-endings", no_argument, 0, 'i'},
                                               {"no-ignore-line-endings", no_argument, 0, 'I'},
                                               {"ignore-whitespace", no_argument, 0, 'w'},
                                               {"no-ignore-whitespace", no_argument, 0, 'W'},
                                               {"list-colors", no_argument, 0, '1'},
                                               {0, 0, 0, 0}};
        int c = 0, option_index = 0;
        while ((c = getopt_long(in_argc, in_argv, "a:hlsS:uU:W:o:n:iIwW", long_options, &option_index)) >= 0) {
            switch (c) {
                case 'v':
                    fmt::print("version: {}\n", DIFFY_VERSION);
                    fmt::print("vcs hash: {}\n", DIFFY_BUILD_HASH);
                    exit(0);
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
                case 'I':
                    opts.ignore_line_endings = false;
                    break;
                case 'w':
                    opts.ignore_whitespace = true;
                    break;
                case 'W': {
                    if (optarg) {
                        if (isdigit(optarg[0])) {
                            opts.width = atoi(optarg);
                        } else {
                            opts.ignore_whitespace = false;
                            optind--;
                        }
                    } else {
                        // Only happens when we don't provide any positional arguments
                        opts.ignore_whitespace = false;
                    }
                    break;
                }
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

        if (0)
        {
            char** envp = environ;
            while (*envp != NULL) {
                printf("%s\n", *envp);
                envp++;
            }
        }

        if (0)
        {
            for (int i = optind; i < argc; i++) {
                printf("arg %d: %s\n", i, argv[i]);
            }
        }

        opts.left_file = argv[optind];
        opts.right_file = argv[optind + 1];

        auto a_status = diffy::check_file_status(opts.left_file);
        auto b_status = diffy::check_file_status(opts.right_file);
        
        // When invoked as a ´git difftool´, handle cases where files are added or deleted.
        // TODO: maybe this can be written in a better way.
        if (invoked_as_git_tool) {
            const char* git_base_nully = getenv("BASE");
            std::string git_base = git_base_nully != nullptr ? git_base_nully : "";
            auto git_base_permissions = diffy::read_file_permissions(git_base);

            if (a_status == diffy::FileStatus::kOk && b_status == diffy::FileStatus::kNullPath) {
                // left file ok, right file null: deleted file
                opts.right_file_name = "";
                opts.left_file_name = git_base;
                opts.left_file_permissions = diffy::read_file_permissions(opts.left_file);
                opts.right_file_permissions = std::nullopt;
            } else if (a_status == diffy::FileStatus::kNullPath && b_status == diffy::FileStatus::kOk) {
                // left file null, right file ok: added file
                opts.left_file_name = "";
                opts.right_file_permissions = std::nullopt;
                opts.right_file_name = git_base;
                opts.right_file_permissions = git_base_permissions;
            } else if (a_status == diffy::FileStatus::kOk && b_status == diffy::FileStatus::kOk) {
                // both file ok: changed file
                opts.left_file_permissions = diffy::read_file_permissions(git_base);
                opts.left_file_name = git_base;
                opts.right_file_permissions = diffy::read_file_permissions(opts.right_file_name);
                opts.right_file_name = git_base;
            } else {
                assert(0 && "Both files are invalid");
                exit(1);
            }
        } else {
            // Use optional names
            if (opts.left_file_name.empty()) {
                opts.left_file_name = opts.left_file;
            }

            if (opts.right_file_name.empty()) {
                opts.right_file_name = opts.right_file;
            }

            opts.left_file_permissions = diffy::read_file_permissions(opts.left_file_name);
            opts.right_file_permissions = diffy::read_file_permissions(opts.right_file_name);

            // Null paths will be readable (TODO: test on windows)
            auto a_valid = a_status == diffy::FileStatus::kOk || a_status == diffy::FileStatus::kNullPath;
            auto b_valid = b_status == diffy::FileStatus::kOk || b_status == diffy::FileStatus::kNullPath;
            if (!a_valid || !b_valid) {
                std::string err;
                if (!a_valid)
                    err += fmt::format("File A '{}': {}\n", opts.left_file, diffy::to_string(a_status));
                if (!b_valid)
                    err += fmt::format("File B '{}': {}\n", opts.right_file, diffy::to_string(b_status));
                show_help(err);
                return false;
            }
            return true;
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

    if (result.status != diffy::DiffResultStatus::OK && result.status != diffy::DiffResultStatus::NoChanges) {
        puts("Diff compute failed");
        return 1;
    }

    auto hunks = diffy::compose_hunks(result.edit_sequence, opts.context_lines);

    if (opts.column_view) {
        const auto& annotated_hunks = annotate_hunks(
            diff_input, hunks,
            opts.line_granularity ? diffy::EditGranularity::Line : diffy::EditGranularity::Token,
            opts.ignore_whitespace);
        diffy::column_view_diff_render(diff_input, annotated_hunks, cv_ui_opts, opts);
    } else if (opts.unified) {
        auto unified_lines = diffy::unified_diff_render(diff_input, hunks);
        auto num_lines = unified_lines.size();
        for (auto i = 0u; i < num_lines; i++) {
            const auto& line = unified_lines[i];
            if (line[line.size() - 1] == '\n')
                printf("%s", line.c_str());
            else {
                printf("%s\n", line.c_str());
                if (i == num_lines-1)
                    printf("\\ No newline at end of file\n");
            }
        }
        return 1;
    }

    return 0;
}
