#pragma once

#include "algorithms/algorithm.hpp"  // Algo, algo_from_string
#include "util/color.hpp"

#include <string>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include <config_parser/config_parser.hpp>

namespace diffy {

// --color: when to emit ANSI colour in the unified output. Auto follows the
// tty (colour to a terminal, plain to a pipe/file so it round-trips through
// `patch`); Always/Never force it either way (e.g. for golden snapshots).
enum class ColorMode { Auto, Always, Never };

// --binary/--text: whether to treat the inputs as binary and show a hex diff.
// Auto sniffs each file for NUL bytes; Always/Never force the decision.
enum class BinaryMode { Auto, Always, Never };

// --image/--no-image: whether image files get an image diff (currently a
// metadata diff) instead of a hex dump. Auto detects by magic bytes.
enum class ImageMode { Auto, Always, Never };

// --image-render/--no-image-render: whether to draw the image diff inline in the
// terminal (half-block art). Auto = when stdout is a tty; Always forces it even
// to a pipe (testing); Never keeps just the text summary.
enum class ImageRenderMode { Auto, Always, Never };

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

    bool ignore_line_endings = false;
    bool ignore_whitespace = false;
    bool syntax_highlight = true;  // tree-sitter syntax highlighting (--no-highlight)

    // --language / -L: force the syntax language for both sides instead of
    // detecting it from the file names. Empty means "detect" (the default).
    std::string force_language;

    // --color=auto|always|never: whether to emit ANSI colour in unified output.
    ColorMode color_mode = ColorMode::Auto;

    // --binary/--text: hex diff mode. Auto detects binary inputs by NUL sniff.
    BinaryMode binary_mode = BinaryMode::Auto;
    // --image/--no-image: image diff (metadata) for image files. Auto by magic.
    ImageMode image_mode = ImageMode::Auto;
    // --image-render/--no-image-render: inline terminal rendering of the diff.
    ImageRenderMode image_render = ImageRenderMode::Auto;
    // --image-protocol: force a specific inline protocol ("halfblock"/"kitty"/
    // "iterm2"/"sixel"); empty or "auto" = detect from the terminal.
    std::string image_protocol;
    // --bytes-per-row: bytes shown per hex row. 0 means auto: side-by-side picks
    // the largest multiple of 8 that fits the terminal; unified uses 16.
    int64_t bytes_per_row = 0;
    // --hex-global: byte-align whole files instead of chunking first (small files).
    bool hex_global = false;

    std::string left_file;
    std::string right_file;

    std::optional<std::filesystem::perms> left_file_permissions;
    std::optional<std::filesystem::perms> right_file_permissions;

    std::string left_file_name;
    std::string right_file_name;
};

struct ColumnViewTextStyle {
    // clang-format off
    // Base background painted under every cell. Specific styles below layer on
    // top, so a fully inverted ("filled background") theme can be expressed by
    // setting this single key. kNone by default → no background, byte-identical
    // to the historical fg-only rendering.
    TermStyle background = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };

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

    TermStyle empty_cell = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };

    // Moved-block accent (GAP-9): a relocated line's number is recoloured to this
    // foreground. Left as kNone so a theme that omits `moved_line` produces an
    // empty escape code and the frontend falls back to the built-in violet
    // (#a371f7); a theme sets it to make the moved accent match its palette.
    TermStyle moved_line = TermStyle {
        TermColor::kNone,
        TermColor::kNone,
        TermStyle::Attribute::None
    };
    // clang-format on
};

struct ColumnViewTextStyleEscapeCodes {
    std::string background;
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
    std::string empty_cell;
    std::string moved_line;
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
    // Which built-in syntax palette (light vs dark) to fall back to for groups
    // the theme doesn't override. Inferred from the theme background by
    // config_apply_theme.
    bool light_theme = false;
};

std::string
config_get_directory();

// The bundled, self-contained example themes seeded on first-run setup, as
// {file-stem, .conf-content} pairs (e.g. {"theme_paper", "..."}). Exposed so
// tests can validate the shipped theme content.
std::vector<std::pair<std::string, std::string>>
config_bundled_themes();

// The human-facing display name a theme declares via `[meta] name = '...'`.
// Returns nullopt when the text doesn't parse or has no meta.name (callers then
// fall back to a prettified file stem). Works on any theme .conf text — bundled
// or a user file read from disk.
std::optional<std::string>
config_theme_display_name(const std::string& conf_text);

void
config_apply_options(diffy::ProgramOptions& program_options);

// Read highlight.extensions from diffy.conf and install it as the
// extension→grammar override map (see language_set_overrides). Entries map a
// grammar name to one extension/filename or an array of them, e.g.:
//   [highlight]
//   extensions = { cpp = ['.tpp', '.ixx'], zig = '.zig' }
// (zig here would be a drop-in: zig.dll + zig.scm in <config>/grammars/.)
// Safe to call when the file or section is missing (no-op).
void
config_apply_highlight_overrides();

void
config_apply_theme(const std::string& theme,
                   diffy::ColumnViewCharacters& cv_char_opts,
                   diffy::ColumnViewSettings& cv_view_opts,
                   diffy::ColumnViewTextStyle& cv_style_opts,
                   diffy::ColumnViewTextStyleEscapeCodes& cv_style_escape_codes);

}  // namespace diffy
