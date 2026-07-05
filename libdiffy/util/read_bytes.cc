#include "util/read_bytes.hpp"

#include <cstdio>

namespace diffy {

bool
read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();

    FILE* stream = fopen(path.c_str(), "rb");
    if (!stream) {
        return false;
    }

    // FIFOs / process substitution (e.g. git difftool) aren't seekable, so read
    // in blocks and grow rather than stat-and-slurp.
    unsigned char buffer[64 * 1024];
    size_t nread = 0;
    while ((nread = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        out.insert(out.end(), buffer, buffer + nread);
    }

    const bool ok = ferror(stream) == 0;
    fclose(stream);
    if (!ok) {
        out.clear();
    }
    return ok;
}

}  // namespace diffy
