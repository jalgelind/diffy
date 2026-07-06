#include "algorithms/myers_greedy.hpp"
#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "config/config.hpp"
#include "highlight/language.hpp"
#include "highlight/scope.hpp"
#include "highlight/syntax_highlighter.hpp"
#include "binary/hex_align.hpp"
#include "image/decode.hpp"
#include "image/image_diff.hpp"
#include "image/image_info.hpp"
#include "image/term_image.hpp"
#include "output/column_view.hpp"
#include "output/hex_column.hpp"
#include "output/hex_unified.hpp"
#include "output/unified.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "processing/tokenizer.hpp"
#include "util/binary_detect.hpp"
#include "util/color.hpp"
#include "util/hash.hpp"
#include "util/mapped_file.hpp"
#include "util/readlines.hpp"
#include "tty.hpp"

#include <musl/getopt.h>
#include <sys/stat.h>

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gsl/span>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(DIFFY_PLATFORM_WINDOWS)
#include <io.h>  // _isatty / _fileno
#else
#include <unistd.h>  // isatty
#endif

namespace fs = std::filesystem;

namespace {
// True when standard output is an interactive terminal (not a pipe/file), so the
// CLI colourises for viewing but stays plain + patch-compatible when redirected.
bool
stdout_is_tty() {
#if defined(DIFFY_PLATFORM_WINDOWS)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}
}  // namespace

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

    // Handled after the theme is applied (below), so the dump reflects the
    // theme's colour map. parse_args only records that it was requested.
    bool list_colors = false;

    auto show_help = [&](const std::string& optional_error_message) {
        // Wrap the built-in grammar names so --language help stays in sync with
        // what diffy actually detects (derived from the language maps).
        std::string lang_help;
        {
            const std::string indent = "                                 ";
            std::string line = indent;
            const auto langs = diffy::language_list();
            for (size_t i = 0; i < langs.size(); ++i) {
                const std::string tok = langs[i] + (i + 1 < langs.size() ? ", " : "");
                if (line.size() > indent.size() && line.size() + tok.size() > 80) {
                    lang_help += line + "\n";
                    line = indent;
                }
                line += tok;
            }
            lang_help += line;
        }

        // Bundled theme names (plus the built-in default) for --theme help. Users
        // may also have custom themes on disk in the config directory.
        std::string theme_help = "theme_default";
        for (const auto& [name, content] : diffy::config_bundled_themes()) {
            (void)content;
            theme_help += ", " + name;
        }

        std::string help = fmt::format((R"(
Usage: {0} [options] left_file right_file

Compare files line by line, side by side

Options:
    -v, --version                show program version and exit
    -a --algorithm [algorithm]   which algorithm to use for line diffing
                                    myers-linear (ml)
                                    myers-greedy (mg)
                                    patience     (p)
    -u, -U [context_lines]       show unified output, optional context line count
    -s, -S [context_lines]       show side-by-side column output, optional context line count

    -o, --old-file               custom name to give the old-file (left)
    -n, --new-file               custom name to give the new-file (right)

    -i, --ignore-line-endings    ignore changes to line endings
    -I, --no-ignore-line-endings inverse of --ignore-line-endings

    -w, --ignore-whitespace      ignore all changes to whitespace
    -W, --no-ignore-whitespace   inverse of --ignore-whitespace

    --disable-syntax-highlighting  turn off tree-sitter syntax highlighting (on by default)
    -L, --language [lang]        force the syntax language instead of detecting it
                                 from the file names. Supported grammars:
{2}
    --theme [name]               use a named theme instead of the configured one.
                                 Bundled: {3}
                                 (plus any custom themes in the config directory)
    --color [when]               colour unified output: auto (default), always, or never

    --list-colors                list all available colors available in the configuration

Binary (hex) diff:
    --binary                     force hex diff (default: auto-detect binary input)
    --text                       force text diff even for binary-looking input
    --bytes-per-row [n]          bytes shown per hex row (default 16)
    --hex-global                 byte-align whole files instead of chunking (small files)
    --image                      force an image diff for the inputs
    --no-image                   never treat image files specially (hex diff them)
    --image-render               draw the image diff inline (half-block art) even to a pipe
    --no-image-render            never draw inline; show only the text summary

Side by side options:
    -l, --line                   line based diff instead of word based diff
    -W [width]                   maximum width in each column
)"),
                                       argv[0], diffy::config_get_directory(), lang_help, theme_help);

        help += "\n";

        help += "Config directory:\n    " + diffy::config_get_directory() + "\n\n";

        if (!optional_error_message.empty()) {
            help += optional_error_message;
        }
        puts(help.c_str());
    };

    // Above the ASCII range so it doesn't collide with the short options.
    constexpr int kOptNoIgnoreWhitespace = 256;
    constexpr int kOptNoHighlight = 257;
    constexpr int kOptTheme = 258;
    constexpr int kOptColor = 260;
    constexpr int kOptBinary = 261;
    constexpr int kOptText = 262;
    constexpr int kOptBytesPerRow = 263;
    constexpr int kOptHexGlobal = 264;
    constexpr int kOptImage = 265;
    constexpr int kOptNoImage = 266;
    constexpr int kOptImageRender = 267;
    constexpr int kOptNoImageRender = 268;

    auto parse_args = [&](int in_argc, char* in_argv[]) {
        static struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"side-by-side", optional_argument, 0, 'S'},
            {"line", optional_argument, 0, 'l'},
            {"unified", optional_argument, 0, 'U'},
            {"version", no_argument, 0, 'v'},
            {"width", optional_argument, 0, 'W'},
            {"algorithm", optional_argument, 0, 'a'},
            {"old-file", optional_argument, 0, 'o'},
            {"new-file", optional_argument, 0, 'n'},
            {"language", required_argument, 0, 'L'},
            {"ignore-line-endings", no_argument, 0, 'i'},
            {"no-ignore-line-endings", no_argument, 0, 'I'},
            {"ignore-whitespace", no_argument, 0, 'w'},
            {"no-ignore-whitespace", no_argument, 0, kOptNoIgnoreWhitespace},
            {"no-highlight", no_argument, 0, kOptNoHighlight},
            {"disable-syntax-highlighting", no_argument, 0, kOptNoHighlight},
            {"theme", required_argument, 0, kOptTheme},
            {"color", required_argument, 0, kOptColor},
            {"colour", required_argument, 0, kOptColor},
            {"binary", no_argument, 0, kOptBinary},
            {"text", no_argument, 0, kOptText},
            {"bytes-per-row", required_argument, 0, kOptBytesPerRow},
            {"hex-global", no_argument, 0, kOptHexGlobal},
            {"image", no_argument, 0, kOptImage},
            {"no-image", no_argument, 0, kOptNoImage},
            {"image-render", no_argument, 0, kOptImageRender},
            {"no-image-render", no_argument, 0, kOptNoImageRender},
            {"list-colors", no_argument, 0, '1'},
            {0, 0, 0, 0}};
        int c = 0, option_index = 0;
        while ((c = getopt_long(in_argc, in_argv, "a:hlsS:uU:W:o:n:L:iIw", long_options, &option_index)) >= 0) {
            switch (c) {
                case 'v':
                    fmt::print("version: {}\n", DIFFY_VERSION);
                    fmt::print("vcs hash: {}\n", DIFFY_BUILD_HASH);
                    exit(0);
                case 'h':
                    opts.help = true;
                    return true;
                case '1':
                    list_colors = true;
                    return true;
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
                case 'L':
                    opts.force_language = optarg ? optarg : "";
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
                    // -W is overloaded: numeric arg sets width, anything else is
                    // the legacy --no-ignore-whitespace (hand the token back).
                    if (optarg && isdigit(optarg[0])) {
                        opts.width = atoi(optarg);
                    } else if (optarg) {
                        opts.ignore_whitespace = false;
                        optind--;
                    }
                    break;
                }
                case kOptNoIgnoreWhitespace:
                    opts.ignore_whitespace = false;
                    break;
                case kOptNoHighlight:
                    opts.syntax_highlight = false;
                    break;
                case kOptTheme:
                    if (optarg && *optarg) {
                        opts.theme = optarg;
                    }
                    break;
                case kOptBinary:
                    opts.binary_mode = diffy::BinaryMode::Always;
                    break;
                case kOptText:
                    opts.binary_mode = diffy::BinaryMode::Never;
                    break;
                case kOptBytesPerRow:
                    if (optarg && isdigit(optarg[0])) {
                        opts.bytes_per_row = atoi(optarg);
                        if (opts.bytes_per_row < 1) {
                            opts.bytes_per_row = 1;
                        }
                    }
                    break;
                case kOptHexGlobal:
                    opts.hex_global = true;
                    break;
                case kOptImage:
                    opts.image_mode = diffy::ImageMode::Always;
                    break;
                case kOptNoImage:
                    opts.image_mode = diffy::ImageMode::Never;
                    break;
                case kOptImageRender:
                    opts.image_render = diffy::ImageRenderMode::Always;
                    break;
                case kOptNoImageRender:
                    opts.image_render = diffy::ImageRenderMode::Never;
                    break;
                case kOptColor: {
                    const std::string when = optarg ? optarg : "";
                    if (when == "auto") {
                        opts.color_mode = diffy::ColorMode::Auto;
                    } else if (when == "always") {
                        opts.color_mode = diffy::ColorMode::Always;
                    } else if (when == "never") {
                        opts.color_mode = diffy::ColorMode::Never;
                    } else {
                        show_help(fmt::format("error: invalid value for --color ({}); "
                                              "expected auto, always, or never\n",
                                              when));
                        return false;
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

    // Load the global defaults (incl. general.theme) before command-line args
    // override them, then apply the resolved theme — so --theme wins over config.
    diffy::config_apply_options(opts);

    if (!parse_args(argc, argv)) {
        return 2;
    }

    if (opts.help) {
        show_help("");
        return 0;
    }

    diffy::ColumnViewState cv_ui_opts;
    diffy::config_apply_theme(opts.theme, cv_ui_opts.chars, cv_ui_opts.settings, cv_ui_opts.style_config,
                              cv_ui_opts.style);

    // --list-colors: report terminal colour support and dump the (now theme-
    // applied) colour map, then exit.
    if (list_colors) {
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
        return 0;
    }

    // Binary / hex diff path. Decide before readlines so binary input never gets
    // line-split. Auto mode sniffs the first 1 KiB of each file for a NUL byte.
    {
        auto sniff_binary = [](const std::string& path) -> bool {
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) {
                return false;
            }
            std::vector<uint8_t> head(1024);
            size_t n = fread(head.data(), 1, head.size(), f);
            fclose(f);
            head.resize(n);
            return diffy::looks_binary(head);
        };

        const bool want_binary =
            opts.binary_mode == diffy::BinaryMode::Always
                ? true
                : opts.binary_mode == diffy::BinaryMode::Never
                      ? false
                      : (sniff_binary(opts.left_file) || sniff_binary(opts.right_file));

        if (want_binary) {
            diffy::FileBytes a_file, b_file;
            if (!a_file.load(opts.left_file) || !b_file.load(opts.right_file)) {
                fmt::print(stderr, "diffy: failed to read binary input\n");
                return 2;
            }
            const auto a_bytes = a_file.bytes();
            const auto b_bytes = b_file.bytes();

            // Image files: show a metadata diff (format + dimensions) rather than
            // a hex dump. This is the always-available floor; a full visual diff
            // is a later step. --image forces it, --no-image opts out.
            {
                const bool a_img = diffy::looks_image(a_bytes);
                const bool b_img = diffy::looks_image(b_bytes);
                const bool auto_img = (a_img || a_bytes.empty()) && (b_img || b_bytes.empty()) &&
                                      (a_img || b_img);
                const bool want_image = opts.image_mode == diffy::ImageMode::Always ||
                                        (opts.image_mode == diffy::ImageMode::Auto && auto_img);
                if (want_image) {
                    if (a_bytes.size() == b_bytes.size() &&
                        (a_bytes.empty() ||
                         std::memcmp(a_bytes.data(), b_bytes.data(), a_bytes.size()) == 0)) {
                        return 0;  // identical
                    }
                    auto describe = [](gsl::span<const uint8_t> bytes) -> std::string {
                        if (bytes.empty()) {
                            return "(absent)";
                        }
                        const diffy::ImageInfo info = diffy::image_probe(bytes);
                        if (!info.ok) {
                            return fmt::format("not an image ({} bytes)", bytes.size());
                        }
                        if (info.width >= 0 && info.height >= 0) {
                            return fmt::format("{} {}x{}", info.format, info.width, info.height);
                        }
                        return info.format;
                    };
                    // Decode both and report a real pixel-level similarity when
                    // dimensions match; otherwise fall back to the metadata diff.
                    const diffy::DecodedImage da = diffy::decode_image(a_bytes);
                    const diffy::DecodedImage db = diffy::decode_image(b_bytes);
                    if (da.ok && db.ok && da.width == db.width && da.height == db.height) {
                        // Decide up front whether we'll draw the overlay inline, so
                        // we only build the (potentially large) overlay bitmap then.
                        diffy::TermEnv tenv;
                        tenv.is_tty = stdout_is_tty();
                        tenv.disabled = opts.image_render == diffy::ImageRenderMode::Never;
                        tenv.force = opts.image_render == diffy::ImageRenderMode::Always;
                        if (const char* t = std::getenv("TERM")) tenv.term = t;
                        if (const char* tp = std::getenv("TERM_PROGRAM")) tenv.term_program = tp;
                        if (const char* kw = std::getenv("KITTY_WINDOW_ID")) tenv.kitty_window_id = kw;
                        const diffy::TermImageProtocol proto = diffy::detect_term_image_protocol(tenv);

                        diffy::ImageDiffOptions dopts;
                        dopts.compute_overlay = proto != diffy::TermImageProtocol::None;
                        const diffy::ImageDiffResult r =
                            diffy::image_diff(da.rgba, db.rgba, da.width, da.height, dopts);
                        const diffy::ImageInfo info = diffy::image_probe(a_bytes);
                        fmt::print("Image files differ: {} {}x{} — {:.2f}% similar ({} of {} pixels "
                                   "differ)\n",
                                   info.format, da.width, da.height, r.similarity * 100.0, r.changed_px,
                                   r.total_px);

                        if (proto != diffy::TermImageProtocol::None && !r.overlay_rgba.empty()) {
                            int th = 0, tw = 0;
                            diffy::tty_get_term_size(&th, &tw);
                            if (tw <= 0) tw = 80;
                            if (th <= 0) th = 24;
                            int rows = th - 2;  // leave room for the summary line
                            if (rows < 1) rows = 1;
                            const std::string art =
                                diffy::render_term_image(proto, r.overlay_rgba, r.width, r.height, tw, rows);
                            if (!art.empty()) {
                                fputs(art.c_str(), stdout);
                            }
                        }
                    } else {
                        fmt::print("Image files differ:\n  --- {}: {}\n  +++ {}: {}\n",
                                   opts.left_file_name, describe(a_bytes), opts.right_file_name,
                                   describe(b_bytes));
                    }
                    return 1;
                }
            }

            diffy::HexAlignParams hp;
            hp.force_global = opts.hex_global;
            bool truncated = false;
            auto alignment = diffy::hex_align(a_bytes, b_bytes, hp, &truncated);

            bool changed = false;
            for (const auto& seg : alignment) {
                if (seg.kind != diffy::HexSegKind::Equal) {
                    changed = true;
                    break;
                }
            }
            if (!changed) {
                return 0;  // identical
            }

            // Bound pathological output: when a large amount of content couldn't
            // be meaningfully aligned (a big region exceeded the byte-refine
            // budget, or the total change is huge), a full hex dump would be
            // hundreds of thousands of useless rows. Summarise instead.
            {
                uint64_t change_bytes = 0;
                for (const auto& seg : alignment) {
                    if (seg.kind != diffy::HexSegKind::Equal) {
                        change_bytes += seg.a_len + seg.b_len;
                    }
                }
                constexpr uint64_t kCoarseCap = 512ull * 1024;      // coarse + this big
                constexpr uint64_t kHardCap = 8ull * 1024 * 1024;   // huge regardless
                if ((truncated && change_bytes > kCoarseCap) || change_bytes > kHardCap) {
                    fmt::print("Binary files {} and {} differ ({} bytes differ; too dissimilar "
                               "to display a hex diff)\n",
                               opts.left_file_name, opts.right_file_name, change_bytes);
                    return 1;
                }
            }

            const bool color = opts.color_mode == diffy::ColorMode::Always ||
                               (opts.color_mode == diffy::ColorMode::Auto && stdout_is_tty());

            auto resolve_width = [&]() -> int64_t {
                int64_t w = opts.width;
                if (w == 0) {
                    int th = 0, tw = 0;
                    diffy::tty_get_term_size(&th, &tw);
                    w = static_cast<int64_t>(tw);
                }
                return w == 0 ? 80 : w;
            };

            if (opts.column_view) {
                for (const auto& line : diffy::hex_column_render(
                         a_bytes, b_bytes, alignment, color ? &cv_ui_opts.style : nullptr,
                         static_cast<int>(opts.bytes_per_row), opts.context_lines, resolve_width())) {
                    puts(line.c_str());
                }
            } else {
                const int64_t bpr = opts.bytes_per_row > 0 ? opts.bytes_per_row : 16;
                const int64_t fill_width = color ? resolve_width() : 0;
                for (const auto& line : diffy::hex_unified_render(
                         a_bytes, b_bytes, opts.left_file_name, opts.right_file_name, alignment,
                         color ? &cv_ui_opts.style : nullptr, static_cast<int>(bpr), opts.context_lines,
                         fill_width)) {
                    puts(line.c_str());
                }
            }

            if (truncated) {
                fmt::print(stderr,
                           "diffy: note: some changed regions were too large to byte-align; "
                           "shown as whole removed/added blocks\n");
            }
            return 1;
        }
    }

    // ignore_whitespace makes line matching whitespace-insensitive at read time, so
    // reindent-only lines share a checksum and the diff treats them as unchanged.
    auto left_line_data = diffy::readlines(opts.left_file, opts.ignore_line_endings, opts.ignore_whitespace);
    auto right_line_data = diffy::readlines(opts.right_file, opts.ignore_line_endings, opts.ignore_whitespace);

    gsl::span<diffy::Line> left_lines{left_line_data};
    gsl::span<diffy::Line> right_lines{right_line_data};

    diffy::DiffInput<diffy::Line> diff_input{left_lines, right_lines, opts.left_file_name,
                                             opts.right_file_name};

    diffy::DiffResult result;
    if (!compute_diff(opts.algorithm, opts.ignore_whitespace, diff_input, &result)) {
        return 2;
    }

    if (result.status != diffy::DiffResultStatus::OK && result.status != diffy::DiffResultStatus::NoChanges) {
        puts("Diff compute failed");
        return 2;
    }

    auto hunks = diffy::compose_hunks(result.edit_sequence, opts.context_lines);

    // Exit status follows `diff`'s convention: 0 = identical, 1 = differences,
    // 2 = error (handled by the early returns above).
    const int exit_code = hunks.empty() ? 0 : 1;

    if (opts.column_view) {
        auto annotated_hunks = annotate_hunks(
            diff_input, hunks,
            opts.line_granularity ? diffy::EditGranularity::Line : diffy::EditGranularity::Token,
            opts.ignore_whitespace);

        // Terminal-width detection lives in the CLI now; the core renderer takes
        // an explicit width so it stays free of any tty dependency.
        int64_t width = opts.width;
        if (width == 0) {
            int term_height = 0, term_width = 0;
            diffy::tty_get_term_size(&term_height, &term_width);
            width = static_cast<int64_t>(term_width);
        }
        // Fall back to 80 columns when there's no tty (e.g. under a debugger).
        if (width == 0) {
            width = 80;
        }

        // Concatenate each side once (readlines keeps the newlines); used for both
        // syntax highlighting and hunk-scope analysis. Language is inferred from
        // the display name.
        std::string a_text, b_text;
        for (const auto& l : left_line_data) a_text += l.line;
        for (const auto& l : right_line_data) b_text += l.line;
        // --language / -L forces both sides; otherwise detect from the file names.
        const auto forced = diffy::language_from_name(opts.force_language);
        const auto lang_a = forced.empty() ? diffy::language_for_path(opts.left_file_name) : forced;
        const auto lang_b = forced.empty() ? diffy::language_for_path(opts.right_file_name) : forced;

        // Syntax highlighting (colour) is optional. No-op when disabled / unknown
        // language / oversized.
        diffy::LineHighlights a_hl, b_hl;
        if (opts.syntax_highlight) {
            a_hl = diffy::highlight_source(a_text, lang_a);
            b_hl = diffy::highlight_source(b_text, lang_b);
        }

        // git-style hunk context (the enclosing definition per hunk) is always
        // computed, so headers carry it regardless of syntax highlighting — like
        // the GUI. Empty when the language is unknown or grammars are unavailable.
        {
            const auto a_outline = diffy::scope_outline(a_text, lang_a);
            const auto b_outline = diffy::scope_outline(b_text, lang_b);
            for (auto& h : annotated_hunks) {
                int64_t a_change = -1, b_change = -1;
                for (const auto& el : h.a_lines)
                    if (el.type == diffy::EditType::Delete) { a_change = el.line_index; break; }
                for (const auto& el : h.b_lines)
                    if (el.type == diffy::EditType::Insert) { b_change = el.line_index; break; }
                h.context = diffy::hunk_context(a_outline, b_outline, a_change, b_change, h.from_start,
                                                h.to_start);
            }
        }

        for (const auto& line : diffy::column_view_render_lines(diff_input, annotated_hunks, cv_ui_opts,
                                                                opts, width, &a_hl, &b_hl)) {
            puts(line.c_str());
        }
    } else if (opts.unified) {
        // Concatenate each side once, for scope analysis and (optional) highlighting.
        std::string a_text, b_text;
        for (const auto& l : left_line_data) a_text += l.line;
        for (const auto& l : right_line_data) b_text += l.line;
        // --language / -L forces both sides; otherwise detect from the file names.
        const auto forced = diffy::language_from_name(opts.force_language);
        const auto lang_a = forced.empty() ? diffy::language_for_path(opts.left_file_name) : forced;
        const auto lang_b = forced.empty() ? diffy::language_for_path(opts.right_file_name) : forced;

        // git-style hunk context for the "@@ ... @@" headers — always computed so
        // the enclosing definition shows regardless of syntax highlighting (colour).
        std::vector<std::string> hunk_contexts;
        {
            const auto a_outline = diffy::scope_outline(a_text, lang_a);
            const auto b_outline = diffy::scope_outline(b_text, lang_b);
            hunk_contexts.reserve(hunks.size());
            for (const auto& h : hunks) {
                int64_t a_change = -1, b_change = -1;
                for (const auto& e : h.edit_units) {
                    if (a_change < 0 && e.type == diffy::EditType::Delete) a_change = e.a_index;
                    if (b_change < 0 && e.type == diffy::EditType::Insert) b_change = e.b_index;
                    if (a_change >= 0 && b_change >= 0) break;
                }
                hunk_contexts.push_back(diffy::hunk_context(a_outline, b_outline, a_change, b_change,
                                                            h.from_start, h.to_start));
            }
        }

        // Colourise for terminal viewing only. --color forces the decision;
        // otherwise (auto) colour a terminal but stay plain to a pipe or file
        // (git difftool, `> foo.patch`, the test suite) so the output round-trips
        // through `patch`.
        const bool color = opts.color_mode == diffy::ColorMode::Always ||
                           (opts.color_mode == diffy::ColorMode::Auto && stdout_is_tty());
        diffy::LineHighlights a_hl, b_hl;
        if (color && opts.syntax_highlight) {
            a_hl = diffy::highlight_source(a_text, lang_a);
            b_hl = diffy::highlight_source(b_text, lang_b);
        }

        // Terminal width, so coloured rows fill to the right edge as solid bars.
        // Honours an explicit -W, else the detected terminal size, else 80.
        int64_t fill_width = 0;
        if (color) {
            fill_width = opts.width;
            if (fill_width == 0) {
                int term_height = 0, term_width = 0;
                diffy::tty_get_term_size(&term_height, &term_width);
                fill_width = static_cast<int64_t>(term_width);
            }
            if (fill_width == 0) {
                fill_width = 80;
            }
        }

        auto unified_lines = diffy::unified_diff_render(
            diff_input, hunks, hunk_contexts.empty() ? nullptr : &hunk_contexts,
            color ? &cv_ui_opts.style : nullptr, color ? &a_hl : nullptr, color ? &b_hl : nullptr,
            cv_ui_opts.settings.light_theme, fill_width);
        auto num_lines = unified_lines.size();
        for (auto i = 0u; i < num_lines; i++) {
            const auto& line = unified_lines[i];
            if (line[line.size() - 1] == '\n')
                printf("%s", line.c_str());
            else {
                printf("%s\n", line.c_str());
            }
        }

        // TODO(ja): This differs from how ´diff´ does it. ´diff´ also warns about missing newline before
        //           eof in the left file. When using this program with git it makes sense to skip this,
        //           since you don't really care if the issue was present in the previous revision.
        bool right_eof_newline = false;
        if (right_line_data.size() > 0) {
            const std::string& last_line = right_line_data.back().line;
            if (!last_line.empty() && last_line.back() == '\n') {
                right_eof_newline = true;
            }
        }

        if (!right_eof_newline) {
            printf("\\ No newline at end of file\n");
        }
    }

    return exit_code;
}
