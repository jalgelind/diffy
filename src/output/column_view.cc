#include "column_view.hpp"

#include "processing/diff_hunk.hpp"
#include "processing/diff_hunk_annotate.hpp"
#include "util/tty.hpp"
#include "util/utf8decode.hpp"

#include <fcntl.h>

#include <fmt/format.h>

#include <cstdlib>
#include <gsl/span>
#include <numeric>
#include <stack>

using namespace diffy;

namespace {

struct DisplayCommand {
    std::string style;
    std::string text;

    static DisplayCommand
    with_style(std::string style, std::string text) {
        return DisplayCommand{.style = style, .text = text};
    }

    static DisplayCommand
    unstyled(std::string text) {
        return DisplayCommand{.style = "", .text = text};
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

enum class ColumnSide {
    Left,
    Right,
};

struct DisplayLineSegment {
    std::string text;
    int64_t text_len;
    TokenFlag flags;
    EditType type;
};

struct DisplayLine {
    std::vector<DisplayLineSegment> segments;
    int64_t line_length = 0;
    int64_t line_number = -1;
    bool is_word_wrapped = false;
    EditType type = EditType::Meta;
};

struct DisplayColumns {
    std::vector<DisplayLine> left;
    std::vector<DisplayLine> right;
    bool show_separator = true;
};

std::vector<DisplayLine>
make_display_line_chopped(const DisplayLine& input_line, int64_t limit) {
    DisplayLine line;
    line.line_number = input_line.line_number;
    line.type = input_line.type;

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
                    const ColumnViewState config) {
    DisplayLine display_line;
    display_line.line_number = static_cast<int>(edit_line.line_index + 1);
    display_line.type = edit_line.type;

    for (const auto& segment : edit_line.segments) {
        std::string text;
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
            }
        }

        display_line.segments.push_back({
            text,
            utf8_len(text),
            segment.flags,
            segment.type,
        });
    }

    display_line.line_length = std::accumulate(
        display_line.segments.begin(), display_line.segments.end(), 0U,
        [](size_t acc, const DisplayLineSegment& segment) { return acc + (size_t) segment.text_len; });

    return display_line;
}

std::vector<DisplayLine>
make_display_lines(const gsl::span<diffy::Line>& content_strings,
                   const EditLine& line,
                   const ColumnViewState& config) {
    DisplayLine display_line = transform_edit_line(content_strings, line, config);

    if (!config.settings.word_wrap) {
        return make_display_line_chopped(display_line, config.max_row_length);
    }

    return make_display_line_wrapped(display_line, config.max_row_length);
}

void
insert_alignment_rows(DisplayColumns& columns) {
    std::size_t ia = 0u, ib = 0u;

    DisplayLine empty;

    while (ia < columns.left.size() || ib < columns.right.size()) {
        auto left_line = ia < columns.left.size() ? columns.left[ia] : empty;
        auto right_line = ib < columns.right.size() ? columns.right[ib] : empty;

        auto left_type = left_line.type;
        auto right_type = right_line.type;

        if (left_type == EditType::Delete && right_type == EditType::Common) {
            columns.right.insert(columns.right.begin() + ia, empty);
            ia -= 2;
            ib -= 2;
        }

        if (right_type == EditType::Insert && left_type == EditType::Common) {
            columns.left.insert(columns.left.begin() + ib,
                                empty);  // TODO: replace `empty` with `{}` and may get stuck in an infinite
                                         // loop. Figure out why.
            ia -= 2;
            ib -= 2;
        }

        ia++;
        ib++;
    }
}

DisplayColumns
make_header_columns(const std::string& left, const std::string right, const ColumnViewState& config) {
    auto shorten = [&config](const std::string& s) {
        if (static_cast<int64_t>(s.size()) > config.max_row_length) {
            return "..." + s.substr(s.size() - config.max_row_length + 3);
        }
        return s;
    };

    auto a = shorten(left);
    auto b = shorten(right);

    auto alen = utf8_len(a);
    auto blen = utf8_len(b);

    return {
        {DisplayLine{{{config.style.header + a, alen, 0, EditType::Meta}}, alen}},
        {DisplayLine{{{config.style.header + b, blen, 0, EditType::Meta}}, blen}},
        false,
    };
}

std::vector<DisplayColumns>
make_display_columns(const DiffInput<diffy::Line>& diff_input,
                     const std::vector<AnnotatedHunk>& hunks,
                     const ColumnViewState& config) {
    std::vector<DisplayColumns> hunk_columns;

    hunk_columns.push_back(make_header_columns(diff_input.A_name, diff_input.B_name, config));

    auto make_rows = [&diff_input, &config](auto side, const auto& lines) {
        std::vector<DisplayLine> rows;
        for (const auto& line : lines) {
            const auto& content_strings = side == ColumnSide::Left ? diff_input.A : diff_input.B;
            const auto& display_lines = make_display_lines(content_strings, line, config);
            for (const auto& display_line : display_lines) {
                rows.push_back(display_line);
            }
        }
        return rows;
    };

    for (const auto& hunk : hunks) {
        DisplayColumns columns{
            // TODO: replace ColumnSide with diff_input.A/B
            make_rows(ColumnSide::Left, hunk.a_lines),
            make_rows(ColumnSide::Right, hunk.b_lines),
        };

        insert_alignment_rows(columns);

        // @cleanup
        auto diff = static_cast<int64_t>(columns.left.size()) - static_cast<int64_t>(columns.right.size());

        if (diff < 0) {
            for (int i = 0; i < -diff; i++) {
                columns.left.push_back({});
            }
        } else if (diff > 0) {
            for (int i = 0; i < diff; i++) {
                columns.right.push_back({});
            }
        }

        assert(columns.left.size() == columns.right.size());

        hunk_columns.push_back(columns);
    }

    return hunk_columns;
}

// Transform the segments into a list of display commands, i.e "set color", "write text"
void
render_display_line(const ColumnViewState& config,
                    std::vector<DisplayCommand>* output,
                    const DisplayLine& line) {
    for (const auto& segment : line.segments) {
        std::string style = "";
        switch (segment.type) {
            case EditType::Insert:
                style = config.style.insert_token;
                break;
            case EditType::Delete:
                style = config.style.delete_token;
                break;
            case EditType::Common:
                style = config.style.common_line;
                break;
            case EditType::Meta:
                // TODO: We need DisplayType or something, "Meta" is a hack that doesn't scale
                // style = config.style.header;
                break;
            default:
                break;
        }
        output->push_back(DisplayCommand::with_style(style, segment.text));
    }
}

void
print_display_columns_tty(const std::vector<DisplayColumns>& columns, const ColumnViewState& config) {
    auto print_display_lines = [](const DisplayLine& left, const DisplayLine& right,
                                  const ColumnViewState& config) {
        std::vector<DisplayCommand> display_commands;

        // color stack? do we have the line meta data somewhere here to decide if we should
        // push header_background or content_background?

        // Left side
        display_commands.push_back(DisplayCommand::unstyled(config.chars.edge_separator));
        if (config.settings.show_line_numbers) {
            std::string style = "";
            if (config.settings.word_wrap) {
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
                        // ??
                        break;
                    default:
                        break;
                }
            }
            display_commands.push_back(DisplayCommand::with_style(
                style, format_line_number(left.line_number, config.line_number_digits_count,
                                          config.settings.line_number_align_right)));
            display_commands.push_back(DisplayCommand::unstyled(" "));
        }

        render_display_line(config, &display_commands, left);
        assert(config.max_row_length >= left.line_length);

        display_commands.push_back(DisplayCommand::with_style(
            config.style.common_line, std::string(config.max_row_length - left.line_length, ' ')));

        // Middle
        if (config.settings.context_colored_line_numbers) {
            display_commands.push_back(
                DisplayCommand::with_style(config.style.frame, config.chars.column_separator));
        } else {
            display_commands.push_back(DisplayCommand::unstyled(config.chars.column_separator));
        }

        // Right side

        if (config.settings.show_line_numbers) {
            std::string style = "";
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
                        // ??
                        break;
                    default:
                        break;
                }
            }
            display_commands.push_back(DisplayCommand::with_style(
                style, format_line_number(right.line_number, config.line_number_digits_count,
                                          config.settings.line_number_align_right)));
            display_commands.push_back(DisplayCommand::unstyled(" "));
        }

        render_display_line(config, &display_commands, right);
        assert(config.max_row_length >= right.line_length);

        display_commands.push_back(DisplayCommand::with_style(
            config.style.common_line, std::string(config.max_row_length - right.line_length, ' ')));

        display_commands.push_back(DisplayCommand::unstyled(config.chars.edge_separator));

        std::string full;
        for (auto& command : display_commands) {
            if (command.style.empty()) {
                full += command.text;
            } else {
                full += command.style + command.text + "\033[0m";
            }
        }
        puts(full.c_str());
    };  // print_display_lines

    for (const auto& column : columns) {
        auto max_idx = std::max(column.left.size(), column.right.size());
        for (size_t idx = 0; idx < max_idx; idx++) {
            print_display_lines(column.left[idx], column.right[idx], config);
        }
        if (column.show_separator) {
            // Empty line between hunks.
            // TODO: configurable somehow
            DisplayLine dl;
            print_display_lines(dl, dl, config);
        }
    }
}

}  // namespace

void
diffy::column_view_diff(const DiffInput<diffy::Line>& diff_input,
                         const std::vector<AnnotatedHunk>& hunks,
                         ColumnViewState& config,
                         int64_t width) {
    if (hunks.size() == 0) {
        // TODO: Should return some sort of failure?
        return;
    }

    int dummy_height = 0;
    if (width == 0) {
        int tmp_width = 0;
        get_term_size(&dummy_height, &tmp_width);
        width = static_cast<int64_t>(tmp_width);
    }

    // If we fail to figure out the width of the terminal, default to 80.
    // NOTE: We hit this when running in lldb.
    if (width == 0) {
        width = 80;
    }

    int64_t frame_characters = 0;
    if (!config.chars.column_separator.empty()) {
        frame_characters += utf8_len(config.chars.column_separator);
    }
    if (!config.chars.edge_separator.empty()) {
        frame_characters += 2 * utf8_len(config.chars.edge_separator);
    }

    int64_t line_number_digits = 4;
    int64_t line_number_digits_padding = 0;
    if (config.settings.show_line_numbers) {
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
    config.max_row_length = (width - extra_layout_characters) / 2;
    if (config.max_row_length < 5)
        config.max_row_length = 5;

    auto display_columns = make_display_columns(diff_input, hunks, config);

    print_display_columns_tty(display_columns, config);
}
