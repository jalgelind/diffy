#include "column_view.hpp"

#include "highlight/highlight_palette.hpp"
#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/utf8decode.hpp"

#include <fcntl.h>

#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>
#include <gsl/span>
#include <numeric>
#include <stack>
#include <tuple>

namespace fs = std::filesystem;

using namespace diffy;

namespace {

struct DisplayCommand {
    std::string style;
    std::string text;

    static DisplayCommand
    with_style(std::string style, std::string text) {
        return DisplayCommand{.style = std::move(style), .text = std::move(text)};
    }
};

std::string
format_line_number(int64_t line_number, int64_t width, bool right_align) {
    if (line_number == -1) {
        return fmt::format("{:>{}}", " ", width);
    }
    if (right_align) {
        return fmt::format("{:>{}}", line_number, width);
    }

    return fmt::format("{:<{}}", line_number, width);
}

struct DisplayLineSegment {
    std::string text;
    int64_t text_len;
    TokenFlag flags;
    EditType type;
    HighlightGroup syntax = HighlightGroup::None;
};

// Truecolor SGR foreground for a syntax group in the chosen palette (light vs
// dark). Empty for None. Escapes carry no display width, so adding them never
// disturbs alignment.
std::string
syntax_fg(HighlightGroup group, bool light) {
    if (group == HighlightGroup::None) {
        return "";
    }
    const HlRgb c = syntax_color(group, light);
    return fmt::format("\033[38;2;{};{};{}m", c.r, c.g, c.b);
}

// Append `text` (whose first byte sits at line column `base_col`) to `out`,
// split at the boundaries of the line's syntax-highlight runs so each piece
// carries one HighlightGroup. With no runs it appends a single None segment.
void
push_text_segments(std::vector<DisplayLineSegment>& out,
                   const std::string& text,
                   uint32_t base_col,
                   TokenFlag flags,
                   EditType type,
                   const std::vector<HighlightRun>* runs) {
    auto emit = [&](uint32_t a, uint32_t b, HighlightGroup g) {
        if (b <= a) {
            return;
        }
        std::string piece = text.substr(a - base_col, b - a);
        out.push_back({piece, utf8_len(piece), flags, type, g});
    };
    if (!runs) {
        emit(base_col, base_col + static_cast<uint32_t>(text.size()), HighlightGroup::None);
        return;
    }
    const uint32_t seg_start = base_col;
    const uint32_t seg_end = base_col + static_cast<uint32_t>(text.size());
    uint32_t pos = seg_start;
    for (const auto& r : *runs) {
        if (r.end <= seg_start) {
            continue;
        }
        if (r.start >= seg_end) {
            break;
        }
        const uint32_t rs = r.start > seg_start ? r.start : seg_start;
        const uint32_t re = r.end < seg_end ? r.end : seg_end;
        if (rs > pos) {
            emit(pos, rs, HighlightGroup::None);
        }
        emit(rs, re, r.group);
        pos = re;
    }
    if (pos < seg_end) {
        emit(pos, seg_end, HighlightGroup::None);
    }
}

struct DisplayLine {
    std::vector<DisplayLineSegment> segments;
    int64_t line_length = 0;
    int64_t line_number = -1;
    bool is_word_wrapped = false;
    EditType type = EditType::Meta;
    int move_id = 0;  // non-zero => a relocated (moved) line; tint its number (GAP-9)
};

// Fallback foreground SGR that flags a moved line's number (violet #a371f7),
// used when a theme leaves `style.moved_line` unset. Layered after the number's
// own style so it keeps the background but recolours the digits.
constexpr const char* kMovedNumberFg = "\033[38;2;163;113;247m";

using DisplayColumns = std::vector<std::vector<DisplayLine>>;

std::vector<DisplayLine>
make_display_line_chopped(const DisplayLine& input_line, int64_t limit) {
    DisplayLine line;
    line.line_number = input_line.line_number;
    line.type = input_line.type;
    line.move_id = input_line.move_id;

    int64_t pos = 0;
    for (const auto& input_segment : input_line.segments) {
        DisplayLineSegment segment = input_segment;
        if (pos + segment.text_len >= limit) {
            auto offset = utf8_advance_by(segment.text, 0, limit - pos);
            // @todo I think we could maybe use string_views instead of substr. Although I don't
            // think we'd notice much of a speedup.
            auto partial = segment.text.substr(0, offset);
            line.segments.push_back({partial, utf8_len(partial), segment.flags, segment.type});
            break;
        } else {
            line.segments.push_back(segment);
        }
        pos += segment.text_len;
    }

    line.line_length = std::accumulate(
        line.segments.begin(), line.segments.end(), 0U,
        [](size_t acc, const DisplayLineSegment& segment) { return acc + (size_t) segment.text_len; });

    return {line};
}

std::vector<DisplayLine>
make_display_line_wrapped(const DisplayLine& input_line, int64_t limit) {
    std::vector<DisplayLine> lines;

    DisplayLine line;
    line.line_number = input_line.line_number;
    line.type = input_line.type;
    line.move_id = input_line.move_id;

    auto segments = input_line.segments;
    auto segments_count = segments.size();
    for (size_t seg_idx = 0; seg_idx < segments_count; seg_idx++) {
        auto& segment = segments[seg_idx];

        if (line.line_length + segment.text_len <= limit) {
            line.segments.push_back(segment);
            line.line_length += segment.text_len;
        } else {
            DisplayLineSegment partial = {"", 0, segment.flags, segment.type};
            auto fits = limit - line.line_length;
            auto offs = utf8_advance_by(segment.text, 0, fits);
            partial.text = segment.text.substr(0, offs);
            partial.text_len = utf8_len(partial.text);
            assert(partial.text_len > 0);

            line.segments.push_back(partial);
            line.line_length += partial.text_len;

            segment.text = segment.text.substr(offs);
            segment.text_len -= partial.text_len;

            seg_idx--;
        }

        assert(line.line_length <= limit);
        assert(line.segments.size() > 0);
        if (line.line_length == limit || seg_idx == segments_count - 1) {
            lines.push_back(line);
            line = {};
            line.type = input_line.type;
        line.move_id = input_line.move_id;
            line.is_word_wrapped = true;
        }
    }

    for (auto i = 0u; i < lines.size(); i++) {
        if (i != 0) {
            assert(lines[i].line_number == -1);
        } else {
            assert(lines[i].line_number > 0);
        }
    }

    return lines;
}

DisplayLine
transform_edit_line(const gsl::span<diffy::Line>& content_strings,
                    const EditLine& edit_line,
                    const ColumnViewState& config,
                    const std::vector<HighlightRun>* runs) {
    DisplayLine display_line;
    display_line.line_number = static_cast<int>(edit_line.line_index + 1);
    display_line.type = edit_line.type;
    display_line.move_id = edit_line.move_id;

    for (const auto& segment : edit_line.segments) {
        std::string text;
        bool plain = false;  // verbatim source text (eligible for syntax splitting)
        if (segment.type != EditType::Common) {
            if (segment.flags & TokenFlagTab) {
                for (size_t i = 0; i < segment.length; i++)
                    text += config.chars.tab_replacement;
            } else if (segment.flags & TokenFlagCR) {
                for (size_t i = 0; i < segment.length; i++)
                    text += config.chars.cr_replacement;
            } else if (segment.flags & TokenFlagSpace) {
                for (size_t i = 0; i < segment.length; i++)
                    text += config.chars.space_replacement;
            } else if (segment.flags & TokenFlagLF) {
                for (size_t i = 0; i < segment.length; i++)
                    text += config.chars.lf_replacement;
            } else if (segment.flags & TokenFlagCRLF) {
                for (size_t i = 0; i < segment.length / 2; i++)
                    text += config.chars.crlf_replacement;
            } else {
                auto idx = static_cast<long>(edit_line.line_index);
                text = content_strings[idx].line.substr(segment.start, segment.length);
                plain = true;
            }
        } else {
            if (segment.flags & TokenFlagTab) {
                for (size_t i = 0; i < segment.length; i++)
                    text += std::string(utf8_len(config.chars.tab_replacement), ' ');
            } else if (segment.flags & TokenFlagSpace) {
                for (size_t i = 0; i < segment.length; i++)
                    text += " ";
            } else if (segment.flags & TokenFlagCR) {
                text += "";
            } else if (segment.flags & TokenFlagLF) {
                text += "";
            } else if (segment.flags & TokenFlagCRLF) {
                text += "";
            } else {
                auto idx = static_cast<long>(edit_line.line_index);
                text = content_strings[idx].line.substr(segment.start, segment.length);
                plain = true;
            }
        }

        // Verbatim text (any edit type) is split at syntax-run boundaries so it
        // carries its HighlightGroup: unchanged lines and — since UXP-9 — changed
        // ones too, so added/deleted code stays syntax-coloured (the token
        // background marks the change). Whitespace markers stay plain (None).
        if (plain && runs) {
            push_text_segments(display_line.segments, text, segment.start, segment.flags, segment.type,
                               runs);
        } else {
            display_line.segments.push_back({
                text,
                utf8_len(text),
                segment.flags,
                segment.type,
                HighlightGroup::None,
            });
        }
    }

    display_line.line_length = std::accumulate(
        display_line.segments.begin(), display_line.segments.end(), 0U,
        [](size_t acc, const DisplayLineSegment& segment) { return acc + (size_t) segment.text_len; });

    return display_line;
}

std::vector<DisplayLine>
make_display_lines(const gsl::span<diffy::Line>& content_strings,
                   const EditLine& line,
                   const ColumnViewState& config,
                   const std::vector<HighlightRun>* runs) {

    // TODO: So... this is the one that works on line level...
    DisplayLine display_line = transform_edit_line(content_strings, line, config, runs);

    if (!config.settings.word_wrap) {
        return make_display_line_chopped(display_line, config.max_row_length);
    }

    return make_display_line_wrapped(display_line, config.max_row_length);
}

void
insert_alignment_rows(DisplayColumns& columns) {
    auto& left = columns[0];
    auto& right = columns[1];
    const DisplayLine empty;

    // Pad both columns with blank rows so each display row is one of: two aligned
    // Common lines, a Delete opposite a blank, a blank opposite an Insert, or a
    // Delete opposite an Insert (a changed line, shown on the same row). Build
    // fresh columns in a single two-pointer pass — the previous in-place version
    // inserted at the wrong index and used `ia -= 2` on unsigned counters, which
    // underflowed at the start of a hunk that began with >= 2 deletes and dropped
    // out of the loop with mismatched, misaligned columns.
    std::vector<DisplayLine> out_left, out_right;
    out_left.reserve(left.size() + right.size());
    out_right.reserve(left.size() + right.size());

    std::size_t ia = 0u, ib = 0u;
    while (ia < left.size() || ib < right.size()) {
        const bool has_l = ia < left.size();
        const bool has_r = ib < right.size();
        const bool l_delete = has_l && left[ia].type == EditType::Delete;
        const bool r_insert = has_r && right[ib].type == EditType::Insert;

        if (l_delete && !r_insert) {
            // A deletion with no insertion to pair with: blank on the right.
            out_left.push_back(left[ia]);
            out_right.push_back(empty);
            ia++;
        } else if (r_insert && !l_delete) {
            // An insertion with no deletion to pair with: blank on the left.
            out_left.push_back(empty);
            out_right.push_back(right[ib]);
            ib++;
        } else {
            // Both Common (aligned context), a Delete facing an Insert (a changed
            // line on one row), or one side already exhausted.
            out_left.push_back(has_l ? left[ia] : empty);
            out_right.push_back(has_r ? right[ib] : empty);
            ia++;
            ib++;
        }
    }

    left = std::move(out_left);
    right = std::move(out_right);
}

std::string
to_file_permission_string(const std::filesystem::perms perms) {
    std::string result;
    auto p = perms;

    result += "u:";
    result += ((p & fs::perms::owner_read) != fs::perms::none) ? "r" : "-";
    result += ((p & fs::perms::owner_write) != fs::perms::none) ? "w" : "-";
    result += ((p & fs::perms::owner_exec) != fs::perms::none) ? "x" : "-";
    
    result += " g:";
    result += ((p & fs::perms::group_read) != fs::perms::none) ? "r" : "-";
    result += ((p & fs::perms::group_write) != fs::perms::none) ? "w" : "-";
    result += ((p & fs::perms::group_exec) != fs::perms::none) ? "x" : "-";
    
    result += " o:";
    result += ((p & fs::perms::others_read) != fs::perms::none) ? "r" : "-";
    result += ((p & fs::perms::others_write) != fs::perms::none) ? "w" : "-";
    result += ((p & fs::perms::others_exec) != fs::perms::none) ? "x" : "-";
    
    return result;
}

static std::tuple<std::string, std::string>
color_code_file_permissions(const std::string& delete_style,
                            const std::string& insert_style,
                            const std::string& normal_style,
                            const std::string& left,
                            const std::string& right) {
    if (left == right || left.size() != right.size()) {
        return std::make_tuple(left, right);
    }

    std::string styled_left, styled_right;

    for (int i = 0; i < left.size(); i++) {
        if (left[i] != right[i]) {
            // Don't highlight deletion when there was no pemission previously
             if (left[i] == '-') {
                styled_left += left[i];
             } else {
                // Reset the style in case we enable any attributes that isn't set in the normal style
                styled_left += delete_style + left[i] + "\033[0m" + normal_style;
             }
            styled_right += insert_style + right[i] + "\033[0m" + normal_style;
        } else {
            styled_left += left[i];
            styled_right += right[i];
        }
    }

    return std::make_tuple(styled_left, styled_right);
}

std::vector<DisplayColumns>
make_header_columns(const std::string& left_name,
                    const std::optional<std::filesystem::perms> a_permissions,
                    const std::string& right_name,
                    const std::optional<std::filesystem::perms> b_permissions,
                    const ColumnViewState& config) {
    std::vector<DisplayLine> col_left;
    std::vector<DisplayLine> col_right;
   
    auto shorten = [&](const std::string& s, int trail_reserved = 0) {
        int max_len = config.max_row_length - trail_reserved;
        if (s.size() > max_len) {
            return "..." + s.substr(s.size() - max_len + 3);
        }
        return s;
    };
    
    // TODO: If permissions are equal, then don't bother showing them.

    std::string left_perm;
    std::string left_perm_long;
    std::string left_perm_short;
    std::string right_perm;
    std::string right_perm_long;
    std::string right_perm_short;

    // r:rw- u:r-x o:-wx
    if (a_permissions) {
        left_perm_short = fmt::format("{:o}", (int) *a_permissions);
        left_perm_long = to_file_permission_string(*a_permissions);
    }
    if (b_permissions) {
        right_perm_short = fmt::format("{:o}", (int) *b_permissions);
        right_perm_long = to_file_permission_string(*b_permissions);
    }

    const int long_perm_width = strlen("u:rw- g:r-- o:r--");
    const int short_perm_width = strlen("644");
    
    const int num_extra_long_and_short_perm_chars = 3; // " ()"

    const bool left_long_perm_fits = left_name.size() + long_perm_width +
        num_extra_long_and_short_perm_chars < config.max_row_length;
    const bool left_short_perm_fits = left_name.size() + short_perm_width +
        num_extra_long_and_short_perm_chars < config.max_row_length;
    
    const bool right_long_perm_fits = right_name.size() + long_perm_width +
        num_extra_long_and_short_perm_chars < config.max_row_length;
    const bool right_short_perm_fits = right_name.size() + short_perm_width +
        num_extra_long_and_short_perm_chars < config.max_row_length;

    int left_perm_width = 0;
    int right_perm_width = 0;
    int num_extra_perm_chars = 0;

    if (left_long_perm_fits && right_long_perm_fits)
    {
        left_perm_width = long_perm_width;
        right_perm_width = long_perm_width;
        left_perm = left_perm_long;
        right_perm = right_perm_long;
        num_extra_perm_chars = 3; // " ()"
    } else if (right_short_perm_fits && left_short_perm_fits) {
        left_perm_width = short_perm_width;
        right_perm_width = short_perm_width;
        left_perm = left_perm_short;
        right_perm = right_perm_short;
        num_extra_perm_chars = 3; // " ()"
    } else {
        // Permissions doesn't fit on one line
        // TODO: try two lines
        left_perm_width = 0;
        right_perm_width = 0;
        left_perm = "";
        right_perm = "";
        num_extra_perm_chars = 0;
    }
    
    auto a = shorten(left_name, left_perm_width + num_extra_perm_chars);
    auto b = shorten(right_name, right_perm_width + num_extra_perm_chars);

    // Display width of the header cell. Count the permission suffix only when it
    // is actually appended below. Otherwise an added/deleted file (empty name +
    // no permissions) over-counts by the reserved perm width, so the cell renders
    // too short and the column separator shifts left for that header row.
    auto alen = utf8_len(a);
    auto blen = utf8_len(b);

    const auto &[left_perm_color, right_perm_color] = color_code_file_permissions(
        config.style.delete_token,
        config.style.insert_token,
        config.style.header,
        left_perm,
        right_perm);

    if (!left_perm_color.empty()) {
        a += fmt::format(" ({})", left_perm_color);
        alen += left_perm_width + num_extra_perm_chars;
    }

    if (!right_perm_color.empty()) {
        b += fmt::format(" ({})", right_perm_color);
        blen += right_perm_width + num_extra_perm_chars;
    }

    col_left.push_back({DisplayLine{{{config.style.header + a + "\033[0m", alen, 0, EditType::Meta}}, alen}});
    col_right.push_back({DisplayLine{{{config.style.header + b + "\033[0m", blen, 0, EditType::Meta}}, blen}});

    return {{ col_left, col_right }};
}

// Build the display rows for a single hunk: both panes, aligned and padded.
DisplayColumns
make_hunk_columns(const DiffInput<diffy::Line>& diff_input,
                  const AnnotatedHunk& hunk,
                  const ColumnViewState& config,
                  const LineHighlights* a_highlights,
                  const LineHighlights* b_highlights) {
    auto make_rows = [&config](const auto& content_strings, const auto& lines,
                               const LineHighlights* hl) {
        std::vector<DisplayLine> rows;
        for (const auto& line : lines) {
            const std::vector<HighlightRun>* runs = nullptr;
            if (hl && line.line_index < hl->size() && !(*hl)[line.line_index].empty()) {
                runs = &(*hl)[line.line_index];
            }
            const auto& display_lines = make_display_lines(content_strings, line, config, runs);
            for (const auto& display_line : display_lines) {
                rows.push_back(display_line);
            }
        }

        if (rows.empty()) {
            // Empty line between hunks
            // TODO(ja): doesn't work
            rows.push_back(DisplayLine{});
        }

        return rows;
    };

    DisplayColumns columns{
        make_rows(diff_input.A, hunk.a_lines, a_highlights),
        make_rows(diff_input.B, hunk.b_lines, b_highlights),
    };

    insert_alignment_rows(columns);

    // @cleanup
    auto diff = static_cast<int64_t>(columns[0].size()) - static_cast<int64_t>(columns[1].size());

    if (diff < 0) {
        for (int i = 0; i < -diff; i++) {
            columns[0].push_back({});
        }
    } else if (diff > 0) {
        for (int i = 0; i < diff; i++) {
            columns[1].push_back({});
        }
    }

    assert(columns[0].size() == columns[1].size());

    return columns;
}

// The base style (background tint) for a whole row of the given edit type.
// Used for padding and as the layer under each body segment, so an inverted
// theme fills the full column width with the line's own color. Returns "" for
// the historical fg-only themes (delete_line/insert_line/common_line unset),
// keeping output byte-identical for them.
const std::string&
line_style(const ColumnViewState& config, EditType type) {
    switch (type) {
        case EditType::Insert:
            return config.style.insert_line;
        case EditType::Delete:
            return config.style.delete_line;
        case EditType::Common:
            return config.style.common_line;
        default:
            return config.style.empty_cell;
    }
}

// Transform the segments into a list of display commands, i.e "set color", "write text".
// Each segment is painted with the line's base style (its background) and, for changed
// tokens, the token style layered on top — so token highlights sit over the line tint
// rather than clearing it back to the terminal default.
void
render_display_line(const ColumnViewState& config,
                    std::vector<DisplayCommand>* output,
                    const DisplayLine& line) {
    // Compose the layered token styles once per line, not once per segment: the
    // line's base tint with the token style on top, so highlights sit over the
    // tint instead of clearing it.
    const std::string& base = line_style(config, line.type);
    const std::string insert_style = base + config.style.insert_token;
    const std::string delete_style = base + config.style.delete_token;
    for (const auto& segment : line.segments) {
        // A changed token with a syntax group keeps its token background + bold but
        // takes the syntax foreground (SGR is cumulative, so the trailing fg wins),
        // so added/deleted code reads as syntax-coloured code sitting on the token
        // patch. No syntax group (plain files / highlighting off) => "" => the full
        // token style as before. (UXP-9)
        const std::string syntax = syntax_fg(segment.syntax, config.settings.light_theme);
        switch (segment.type) {
            case EditType::Insert:
                output->push_back(DisplayCommand::with_style(insert_style + syntax, segment.text));
                break;
            case EditType::Delete:
                output->push_back(DisplayCommand::with_style(delete_style + syntax, segment.text));
                break;
            // Common (unchanged) and Meta segments keep the line's own background,
            // with the syntax-highlight foreground layered on top when present.
            // TODO: "Meta" is a hack that doesn't scale; it needs its own DisplayType.
            default:
                output->push_back(DisplayCommand::with_style(base + syntax, segment.text));
                break;
        }
    }
}

// Render one left/right row pair into a single styled string.
std::string
render_display_line_pair(const DisplayLine& left, const DisplayLine& right, const ColumnViewState& config) {
    std::vector<DisplayCommand> display_commands;

    // color stack? do we have the line meta data somewhere here to decide if we should
    // push header_background or content_background?

    // Left side
    display_commands.push_back(
        DisplayCommand::with_style(config.style.empty_cell, config.chars.edge_separator));
    if (config.settings.show_line_numbers) {
        // Fall back to the row's base style (not "") so the number cell still
        // carries a background under an inverted theme when not context-colored.
        std::string style = line_style(config, left.type);
        if (config.settings.context_colored_line_numbers) {
            switch (left.type) {
                case EditType::Insert:
                    style = config.style.insert_line_number;
                    break;
                case EditType::Delete:
                    style = config.style.delete_line_number;
                    break;
                case EditType::Common:
                    style = config.style.common_line_number;
                    break;
                case EditType::Meta:
                    style = config.style.empty_cell;
                    break;
                default:
                    break;
            }
        }
        if (left.move_id != 0) {
            // Theme-driven moved accent (style.moved_line); fall back to the
            // built-in violet when the theme leaves it unset. (GAP-9)
            style += config.style.moved_line.empty() ? kMovedNumberFg : config.style.moved_line;
        }
        display_commands.push_back(DisplayCommand::with_style(
            style, format_line_number(left.line_number, config.line_number_digits_count,
                                      config.settings.line_number_align_right)));
        display_commands.push_back(DisplayCommand::with_style(config.style.empty_cell, " "));
    }

    render_display_line(config, &display_commands, left);
    assert(config.max_row_length >= left.line_length);

    // Pad with the row's own style so a delete/insert highlight spans the full
    // column width, not just the text.
    display_commands.push_back(DisplayCommand::with_style(
        line_style(config, left.type), std::string(config.max_row_length - left.line_length, ' ')));

    // Middle
    if (config.settings.context_colored_line_numbers) {
        display_commands.push_back(
            DisplayCommand::with_style(config.style.frame, config.chars.column_separator));
    } else {
        display_commands.push_back(
            DisplayCommand::with_style(config.style.empty_cell, config.chars.column_separator));
    }

    // Right side

    if (config.settings.show_line_numbers) {
        // Fall back to the row's base style (not "") so the number cell still
        // carries a background under an inverted theme when not context-colored.
        std::string style = line_style(config, right.type);
        if (config.settings.context_colored_line_numbers) {
            switch (right.type) {
                case EditType::Insert:
                    style = config.style.insert_line_number;
                    break;
                case EditType::Delete:
                    style = config.style.delete_line_number;
                    break;
                case EditType::Common:
                    style = config.style.common_line_number;
                    break;
                case EditType::Meta:
                    style = config.style.empty_cell;
                    break;
                default:
                    break;
            }
        }
        if (right.move_id != 0) {
            // Theme-driven moved accent (style.moved_line); fall back to the
            // built-in violet when the theme leaves it unset. (GAP-9)
            style += config.style.moved_line.empty() ? kMovedNumberFg : config.style.moved_line;
        }
        display_commands.push_back(DisplayCommand::with_style(
            style, format_line_number(right.line_number, config.line_number_digits_count,
                                      config.settings.line_number_align_right)));
        display_commands.push_back(DisplayCommand::with_style(config.style.empty_cell, " "));
    }

    render_display_line(config, &display_commands, right);
    assert(config.max_row_length >= right.line_length);

    // Pad with the row's own style so a delete/insert highlight spans the full
    // column width, not just the text.
    display_commands.push_back(DisplayCommand::with_style(
        line_style(config, right.type), std::string(config.max_row_length - right.line_length, ' ')));

    display_commands.push_back(
        DisplayCommand::with_style(config.style.empty_cell, config.chars.edge_separator));

    // A single base background, painted under every cell, lets a fully inverted
    // theme be expressed with one key; per-cell styles layer on top. Empty when
    // unset, so default (fg-only) themes stay byte-identical.
    const std::string& background = config.style.background;

    std::string full;
    for (const auto& command : display_commands) {
        if (command.style.empty() && background.empty()) {
            full += command.text;
        } else {
            full += background;
            full += command.style;
            full += command.text;
            full += "\033[0m";
        }
    }
    return full;
}

// Render every visual row of one DisplayColumns, handing each to `emit`.
template <typename Emit>
void
emit_columns(const DisplayColumns& columns, const ColumnViewState& config, Emit& emit) {
    const auto& left = columns[0];
    const auto& right = columns[1];
    auto max_idx = std::max(left.size(), right.size());
    for (size_t idx = 0; idx < max_idx; idx++) {
        emit(render_display_line_pair(left[idx], right[idx], config));
    }
    // Empty line between hunks.
    // TODO(ja): doesn't work
    if (columns.empty()) {
        DisplayLine dl;
        emit(render_display_line_pair(dl, dl, config));
    }
}

// Render the column view one hunk at a time, handing each row to `emit`. Peak
// memory stays bounded to a single hunk rather than the whole diff.
template <typename Emit>
void
column_view_render_streaming(const DiffInput<diffy::Line>& diff_input,
                             const std::vector<AnnotatedHunk>& hunks,
                             ColumnViewState& config,
                             const ProgramOptions& options,
                             int64_t width,
                             const LineHighlights* a_highlights,
                             const LineHighlights* b_highlights,
                             Emit emit) {
    int64_t frame_characters = 0;
    if (!config.chars.column_separator.empty()) {
        frame_characters += utf8_len(config.chars.column_separator);
    }
    if (!config.chars.edge_separator.empty()) {
        frame_characters += 2 * utf8_len(config.chars.edge_separator);
    }

    int64_t line_number_digits = 4;
    int64_t line_number_digits_padding = 0;
    if (config.settings.show_line_numbers && hunks.size() > 0) {
        auto& last_hunk = *(hunks.end() - 1);
        int64_t line_number_max =
            std::max(last_hunk.from_start + last_hunk.from_count, last_hunk.to_start + last_hunk.to_count);
        int64_t line_number_max_digits = fmt::format("{}", line_number_max + 1).size();
        line_number_digits = line_number_max_digits;
        line_number_digits_padding = 2;
    }

    config.line_number_digits_count = line_number_digits;  // @cleanup

    int64_t extra_layout_characters =
        frame_characters + 2 * (line_number_digits + line_number_digits_padding);

    // TODO: separate row length for left/right
    // TODO: option to use minimum width required?
    config.max_row_length = (width - extra_layout_characters) / 2;
    if (config.max_row_length < 5)
        config.max_row_length = 5;

    // Header first, then each hunk built, emitted, and freed before the next.
    for (const auto& column :
         make_header_columns(diff_input.A_name, options.left_file_permissions, diff_input.B_name,
                             options.right_file_permissions, config)) {
        emit_columns(column, config, emit);
    }

    for (const auto& hunk : hunks) {
        // git-style hunk header, shown as a full-width line above the hunk and
        // mirroring the unified output: the line range, plus —
        // when a caller ran tree-sitter scope analysis — the enclosing definition
        // (e.g. the function the hunk is inside). Painted full-width with the theme
        // background so a light theme doesn't leave a ragged black tail past it.
        {
            const int64_t gutter =
                config.settings.show_line_numbers ? (config.line_number_digits_count + 1) : 0;
            const int64_t row_width = 2 * config.max_row_length + 2 * gutter +
                                      utf8_len(config.chars.column_separator) +
                                      2 * utf8_len(config.chars.edge_separator);
            // Git-style range, matching the unified output's @@ header exactly (a
            // single number when the count is 1, else "start,count").
            auto fmt_change = [](int64_t start, int64_t count) {
                return count == 1 ? fmt::format("{}", start) : fmt::format("{},{}", start, count);
            };
            std::string label = fmt::format("@@ -{} +{} @@", fmt_change(hunk.from_start, hunk.from_count),
                                            fmt_change(hunk.to_start, hunk.to_count));
            if (!hunk.context.empty()) {
                label += " " + hunk.context;
            }
            const int64_t avail = row_width > 0 ? row_width : 0;
            if (static_cast<int64_t>(utf8_len(label)) > avail) {
                label = label.substr(0, utf8_advance_by(label, 0, static_cast<size_t>(avail)));
            }
            const int64_t pad = row_width - static_cast<int64_t>(utf8_len(label));
            emit(config.style.background + config.style.frame + label +
                 (pad > 0 ? std::string(static_cast<size_t>(pad), ' ') : std::string()) + "\033[0m");
        }
        emit_columns(make_hunk_columns(diff_input, hunk, config, a_highlights, b_highlights), config,
                     emit);
    }
}
}  // namespace

std::vector<std::string>
diffy::column_view_render_lines(const DiffInput<diffy::Line>& diff_input,
                                const std::vector<AnnotatedHunk>& hunks,
                                ColumnViewState& config,
                                const diffy::ProgramOptions& options,
                                int64_t width,
                                const LineHighlights* a_highlights,
                                const LineHighlights* b_highlights) {
    std::vector<std::string> out;
    column_view_render_streaming(diff_input, hunks, config, options, width, a_highlights, b_highlights,
                                 [&out](std::string line) { out.push_back(std::move(line)); });
    return out;
}
