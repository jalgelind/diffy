#include "util/binary_detect.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

namespace diffy {

bool
looks_binary(std::string_view data, std::size_t sample) {
    const std::size_t n = std::min(data.size(), sample);
    return n > 0 && std::memchr(data.data(), '\0', n) != nullptr;
}

bool
looks_binary(const std::vector<uint8_t>& data, std::size_t sample) {
    const std::size_t n = std::min(data.size(), sample);
    return n > 0 && std::memchr(data.data(), '\0', n) != nullptr;
}

bool
looks_binary_path(std::string_view path) {
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of('/');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return false;  // no extension (dotfiles / extensionless names aren't assumed binary)
    }
    std::string ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::unordered_set<std::string> binary_exts = {
        ".ldr",  ".bin",  ".o",    ".a",    ".so",   ".dylib", ".dll",  ".exe",  ".lib",
        ".elf",  ".hex",  ".img",  ".dat",  ".wasm", ".pyc",   ".class", ".pdb",
        ".png",  ".jpg",  ".jpeg", ".gif",  ".bmp",  ".ico",   ".webp", ".tiff", ".tif",
        ".pdf",  ".zip",  ".gz",   ".bz2",  ".xz",   ".7z",    ".tar",  ".rar",  ".jar",
        ".mp3",  ".wav",  ".flac", ".ogg",  ".mp4",  ".mov",   ".avi",  ".mkv",  ".webm",
        ".ttf",  ".otf",  ".woff", ".woff2", ".eot",
        ".xlsx", ".xls",  ".docx", ".doc",  ".pptx", ".ppt",   ".odt",  ".ods",
    };
    return binary_exts.count(ext) > 0;
}

}  // namespace diffy
