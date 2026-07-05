#include "unified.hpp"

#include "highlight/highlight_palette.hpp"  // syntax_color
#include "util/utf8decode.hpp"              // utf8_len

#include <sys/stat.h>

#include <algorithm>
#include <fmt/format.h>

#include <cstdlib>
#include <ctime>

using namespace diffy;

// This is wrong; see stackoverflow. There was a link here, but it was lost
// in rebase.
#if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) ||            \
    (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L) || \
    (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 700)
#define DIFFY_NSEC_TIMESTAMPS 1
#else
#define DIFFY_NSEC_TIMESTAMPS 0
#endif

namespace {

// TODO: <filesystem>: http://en.cppreference.com/w/cpp/experimental/fs/last_write_time
bool
get_file_timestamp(const std::string& path, char timestamp[256]) {
    assert(timestamp != nullptr);

    const int MAX_LENGTH = 255;

    // Reproducible-builds override: when SOURCE_DATE_EPOCH is set, pin the header
    // timestamp to it (formatted in UTC) instead of the file's mtime. This makes
    // output deterministic for golden snapshots and works for names that don't
    // exist on disk (synthetic test inputs), where stat() would otherwise fail.
    if (const char* sde = std::getenv("SOURCE_DATE_EPOCH"); sde && *sde) {
        const std::time_t epoch = static_cast<std::time_t>(std::strtoll(sde, nullptr, 10));
        struct tm* gtime = std::gmtime(&epoch);
        if (gtime == nullptr) {
            return false;
        }
        return std::strftime(timestamp, MAX_LENGTH, "%Y-%m-%d %H:%M:%S.000000000 +0000", gtime) > 0;
    }

    struct stat st;
    if (stat(path.c_str(), &st) == -1) {
        return false;
    }

    struct tm* ltime = std::localtime(&st.st_mtime);
    assert(ltime != nullptr);

#if DIFFY_NSEC_TIMESTAMPS
    auto nsec = st.st_mtim.tv_nsec;
#else
    auto nsec = 0;
#endif

    std::string time_format = fmt::format("%Y-%m-%d %H:%M:%S.{:09} %z", nsec);
    return std::strftime(timestamp, MAX_LENGTH, time_format.c_str(), ltime) > 0;
}

// Colourise `body` (no trailing newline) using tree-sitter runs over `base`.
// Runs are ordered, non-overlapping byte ranges; gaps and None runs keep the
// base style, coloured runs get a truecolor foreground and then re-assert `base`
// so its background persists to the end of the line.
std::string
colour_runs(const std::string& body,
            const std::vector<HighlightRun>* runs,
            const std::string& base,
            bool light) {
    if (!runs || runs->empty()) {
        return body;
    }
    std::string out;
    size_t pos = 0;
    for (const auto& run : *runs) {
        if (run.start >= body.size()) {
            break;
        }
        const size_t end = std::min<size_t>(run.end, body.size());
        if (run.start > pos) {
            out += body.substr(pos, run.start - pos);
        }
        if (run.group != HighlightGroup::None) {
            const HlRgb c = syntax_color(run.group, light);
            out += fmt::format("\033[38;2;{};{};{}m", c.r, c.g, c.b);
            out += body.substr(run.start, end - run.start);
            out += base;
        } else {
            out += body.substr(run.start, end - run.start);
        }
        pos = end;
    }
    if (pos < body.size()) {
        out += body.substr(pos);
    }
    return out;
}

// Terminal columns occupied by `s` (which carries no ANSI escapes): UTF-8
// codepoints count as one column each, tabs advance to the next multiple of 8
// (the standard terminal tab stop), matching how the row is actually rendered so
// the background fill lands at the right edge.
std::size_t
display_cols(const std::string& s) {
    std::size_t cols = 0;
    std::size_t seg = 0;
    for (std::size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == '\t') {
            cols += utf8_len(s.substr(seg, i - seg));
            if (i < s.size()) {  // the tab itself
                cols += 8 - (cols % 8);
            }
            seg = i + 1;
        }
    }
    return cols;
}

}  // namespace

std::vector<std::string>
diffy::unified_diff_render(const DiffInput<Line>& diff_input,
                           const std::vector<Hunk>& hunks,
                           const std::vector<std::string>* hunk_contexts,
                           const ColumnViewTextStyleEscapeCodes* style,
                           const LineHighlights* a_hl,
                           const LineHighlights* b_hl,
                           bool light_theme,
                           int64_t fill_width) {
    std::vector<std::string> udiff;

    char timestamp[2][256];
    if (!get_file_timestamp(diff_input.A_name, timestamp[0])) {
        // TODO: Should return error code if this fails
        return udiff;
    }

    if (!get_file_timestamp(diff_input.B_name, timestamp[1])) {
        // TODO: Should return error code if this fails
        return udiff;
    }

    // Colour only when a theme is supplied; the plain path stays byte-identical
    // so it round-trips through `patch`.
    const bool color = style != nullptr;
    const std::string reset = color ? "\033[0m" : "";

    // Pad a line whose visible content occupies `used` columns out to fill_width
    // with spaces, so its background reaches the right edge (solid bars).
    auto fill = [&](std::size_t used) -> std::string {
        if (color && fill_width > 0 && used < static_cast<std::size_t>(fill_width)) {
            return std::string(static_cast<std::size_t>(fill_width) - used, ' ');
        }
        return "";
    };

    {
        std::string a = fmt::format("--- {}\t{}", diff_input.A_name, timestamp[0]);
        std::string b = fmt::format("+++ {}\t{}", diff_input.B_name, timestamp[1]);
        if (color && !style->frame.empty()) {
            udiff.push_back(style->frame + a + fill(display_cols(a)) + reset + "\n");
            udiff.push_back(style->frame + b + fill(display_cols(b)) + reset + "\n");
        } else {
            udiff.push_back(a + "\n");
            udiff.push_back(b + "\n");
        }
    }

    auto format_change = [](const int64_t start, const int64_t count) -> std::string {
        if (count == 1)
            return fmt::format("{}", start);
        return fmt::format("{},{}", start, count);
    };

    for (size_t hi = 0; hi < hunks.size(); ++hi) {
        const auto& hunk = hunks[hi];
        std::string ctx;
        if (hunk_contexts && hi < hunk_contexts->size() && !(*hunk_contexts)[hi].empty()) {
            ctx = " " + (*hunk_contexts)[hi];
        }
        std::string header = fmt::format("@@ -{} +{} @@{}", format_change(hunk.from_start, hunk.from_count),
                                         format_change(hunk.to_start, hunk.to_count), ctx);
        if (color && !style->header.empty()) {
            udiff.push_back(style->header + header + fill(display_cols(header)) + reset + "\n");
        } else {
            udiff.push_back(header + "\n");
        }

        for (const auto& e : hunk.edit_units) {
            const bool from_a = e.a_index.valid;
            std::string& text = from_a ? diff_input.A[static_cast<long>(e.a_index)].line
                                       : diff_input.B[static_cast<long>(e.b_index)].line;
            std::string op = " ";
            if (e.type == EditType::Insert)
                op = "+";
            else if (e.type == EditType::Delete)
                op = "-";

            if (!color) {
                udiff.push_back(fmt::format("{:1}{}", op, text));
                continue;
            }

            // Theme background for the line kind + tree-sitter syntax foreground.
            const std::string& base = e.type == EditType::Insert  ? style->insert_line
                                      : e.type == EditType::Delete ? style->delete_line
                                                                   : style->common_line;
            const LineHighlights* hl = from_a ? a_hl : b_hl;
            const int64_t li =
                from_a ? static_cast<int64_t>(e.a_index) : static_cast<int64_t>(e.b_index);
            const std::vector<HighlightRun>* runs = nullptr;
            if (hl && li >= 0 && static_cast<size_t>(li) < hl->size() && !(*hl)[li].empty()) {
                runs = &(*hl)[li];
            }

            // Keep the trailing newline outside the coloured span so the reset
            // lands before it (no background bleed past the line end).
            std::string body = text;
            std::string nl;
            if (!body.empty() && body.back() == '\n') {
                nl = "\n";
                body.pop_back();
            }
            udiff.push_back(base + op + colour_runs(body, runs, base, light_theme) +
                            fill(1 + display_cols(body)) + reset + nl);
        }
    }

    return udiff;
}
