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

std::vector<std::string>
diffy::unified_diff_render(const DiffInput<Line>& diff_input, const std::vector<Hunk>& hunks) {
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

    udiff.push_back(fmt::format("--- {}\t{}\n", diff_input.A_name, timestamp[0]));
    udiff.push_back(fmt::format("+++ {}\t{}\n", diff_input.B_name, timestamp[1]));

    auto format_change = [](const int64_t start, const int64_t count) -> std::string {
        if (count == 1)
            return fmt::format("{}", start);
        return fmt::format("{},{}", start, count);
    };

    for (const auto& hunk : hunks) {
        udiff.push_back(fmt::format("@@ -{} +{} @@\n", format_change(hunk.from_start, hunk.from_count),
                                    format_change(hunk.to_start, hunk.to_count)));
        for (const auto& e : hunk.edit_units) {
            std::string& text = e.a_index.valid ? diff_input.A[static_cast<long>(e.a_index)].line
                                                : diff_input.B[static_cast<long>(e.b_index)].line;
            std::string op = " ";
            if (e.type == EditType::Insert)
                op = "+";
            else if (e.type == EditType::Delete)
                op = "-";

            udiff.push_back(fmt::format("{:1}{}", op, text));
        }
    }

    return udiff;
}
