#include "log.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace diffy::review {
namespace {

std::mutex&
mutex() {
    static std::mutex m;
    return m;
}

const std::filesystem::path&
path() {
    static const std::filesystem::path p = [] {
        std::error_code ec;
        std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        if (ec) {
            base = std::filesystem::path(".");
        }
        return base / "diffy-review.log";
    }();
    return p;
}

}  // namespace

void
log_line(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex());
    std::ofstream f(path(), std::ios::app);
    if (!f) {
        return;
    }
    std::time_t t = std::time(nullptr);
    char stamp[32] = {0};
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);
    f << stamp << "  " << msg << '\n';
}

std::string
log_path() {
    return path().string();
}

}  // namespace diffy::review
