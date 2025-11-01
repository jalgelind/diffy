#include "unified.hpp"

#include <sys/stat.h>

#include <fmt/format.h>

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

    struct stat st;
    if (stat(path.c_str(), &st) == -1) {
        assert(0);
        return false;
    }

    const int MAX_LENGTH = 255;
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

}  // namespace

bool
diffy::unified_diff_render(const DiffInput<Line>& diff_input,
                           const std::vector<Hunk>& hunks,
                           const std::function<void(std::string_view)>& emit_line) {
    char timestamp[2][256];
    if (!get_file_timestamp(diff_input.A_name, timestamp[0])) {
        // TODO: Should return error code if this fails
        return false;
    }

    if (!get_file_timestamp(diff_input.B_name, timestamp[1])) {
        // TODO: Should return error code if this fails
        return false;
    }

    {
        std::string header = fmt::format("--- {}\t{}\n", diff_input.A_name, timestamp[0]);
        emit_line(header);
    }
    {
        std::string header = fmt::format("+++ {}\t{}\n", diff_input.B_name, timestamp[1]);
        emit_line(header);
    }

    auto format_change = [](const int64_t start, const int64_t count) -> std::string {
        if (count == 1)
            return fmt::format("{}", start);
        return fmt::format("{},{}", start, count);
    };

    for (const auto& hunk : hunks) {
        {
            std::string header = fmt::format(
                "@@ -{} +{} @@\n", format_change(hunk.from_start, hunk.from_count),
                format_change(hunk.to_start, hunk.to_count));
            emit_line(header);
        }
        for (const auto& e : hunk.edit_units) {
            std::string& text = e.a_index.valid ? diff_input.A[static_cast<long>(e.a_index)].line
                                                : diff_input.B[static_cast<long>(e.b_index)].line;
            std::string op = " ";
            if (e.type == EditType::Insert)
                op = "+";
            else if (e.type == EditType::Delete)
                op = "-";

            std::string line = fmt::format("{:1}{}", op, text);
            emit_line(line);
        }
    }

    return true;
}
