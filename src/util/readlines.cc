#include "readlines.hpp"

#include "util/hash.hpp"

#include <string>
#include <vector>
#include <limits.h>
#include <cstdlib>


namespace internal {

bool
getdelim(std::string& lineptr, char delim, FILE* stream) {
    lineptr.reserve(128);

    if (stream == NULL) {
        return false;
    }

    for (;;) {
        char c = getc(stream);

        if (ferror(stream) || c == EOF)
            return false;

        lineptr.push_back(c);

        if (c == delim)
            break;
    }
    return true;
}

bool
getline(std::string& s, FILE* stream) {
    s.clear();
    return internal::getdelim(s, '\n', stream);
}

std::string
right_trim(const std::string& s) {
    auto end = s.find_last_not_of(" \n\r\t\f\v");
    return (end == std::string::npos) ? s : s.substr(0, end + 1);
}
};  // namespace

std::vector<diffy::Line>
diffy::readlines(std::string path, bool ignore_line_endings) {
    std::vector<diffy::Line> lines;

    FILE* stream = fopen(path.c_str(), "rb");
    if (!stream) {
        // NOTE: We check that the file exists as part of argument parsing, so this should
        //       never happen (tm).
        fprintf(stderr, "Failed to open file '%s'\n", path.c_str());
        return lines;
    }

    std::string line;
    uint32_t i = 1;
    while (internal::getline(line, stream)) {
        if (ignore_line_endings) {
            line = internal::right_trim(line);
        }
        uint32_t hash = hash::hash(line.c_str(), static_cast<uint32_t>(line.size()));
        lines.push_back({i, hash, std::move(line)});
        i++;
    }
    fclose(stream);

    return lines;
}