#include "readlines.hpp"

#include "util/hash.hpp"

#include <string>
#include <vector>

// TODO: replace with fstream? unsure why this was done this way.
//       probably needs instream.unsetf(std::ios_base::skipws)
#ifdef DIFFY_PLATFORM_WINDOWS

#include <errno.h>
#include <limits.h>

#include <cstdlib>

using ssize_t = long;

namespace {

ssize_t
getdelim(char** lineptr, size_t* n, int delim, FILE* stream) {
    char c, *cur_pos, *new_lineptr;
    size_t new_lineptr_len;

    if (lineptr == NULL || n == NULL || stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (*lineptr == NULL) {
        *n = 128; /* init len */
        if ((*lineptr = (char*) malloc(*n)) == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }

    cur_pos = *lineptr;
    for (;;) {
        c = getc(stream);

        if (ferror(stream) || (c == EOF && cur_pos == *lineptr))
            return -1;

        if (c == EOF)
            break;

        if ((*lineptr + *n - cur_pos) < 2) {
            if (LONG_MAX / 2 < *n) {
#ifdef EOVERFLOW
                errno = EOVERFLOW;
#else
                errno = ERANGE; /* no EOVERFLOW defined */
#endif
                return -1;
            }
            new_lineptr_len = *n * 2;

            if ((new_lineptr = (char*) realloc(*lineptr, new_lineptr_len)) == NULL) {
                errno = ENOMEM;
                return -1;
            }
            cur_pos = new_lineptr + (cur_pos - *lineptr);
            *lineptr = new_lineptr;
            *n = new_lineptr_len;
        }

        *cur_pos++ = c;

        if (c == delim)
            break;
    }

    *cur_pos = '\0';
    return (ssize_t) (cur_pos - *lineptr);
}

ssize_t
getline(char** lineptr, size_t* n, FILE* stream) {
    return getdelim(lineptr, n, '\n', stream);
}

}  // namespace

#endif

namespace {
std::string
right_trim(const std::string& s) {
    auto end = s.find_last_not_of(" \n\r\t\f\v");
    return (end == std::string::npos) ? s : s.substr(0, end + 1);
}

bool
is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string
strip_whitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (!is_ws(c)) {
            out.push_back(c);
        }
    }
    return out;
}

// The string a line is hashed by — the comparison key. Normally the line itself,
// but whitespace-stripped when whitespace is ignored, so reindent-only lines hash
// equal. The displayed line text is kept separately and is never altered here.
uint32_t
checksum_for(const std::string& line, bool ignore_whitespace) {
    if (ignore_whitespace) {
        std::string key = strip_whitespace(line);
        return hash::hash(key.c_str(), static_cast<uint32_t>(key.size()));
    }
    return hash::hash(line.c_str(), static_cast<uint32_t>(line.size()));
}
};  // namespace

std::vector<diffy::Line>
diffy::readlines(const std::string& path, bool ignore_line_endings, bool ignore_whitespace) {
    std::vector<diffy::Line> lines;

    char* line = nullptr;
    size_t len = 0;
    ssize_t nread;

    FILE* stream = fopen(path.c_str(), "rb");
    if (!stream)
        return lines;  // TODO: Error handling

    uint32_t i = 1;
    while ((nread = getline(&line, &len, stream)) != -1) {
        std::string sline(line);
        if (ignore_line_endings) {
            sline = right_trim(sline);
        }
        uint32_t hash = checksum_for(sline, ignore_whitespace);
        lines.push_back({i, hash, std::move(sline)});
        i++;
    }

    free(line);
    fclose(stream);

    return lines;
}

std::vector<diffy::Line>
diffy::readlines_from_string(const std::string& content, bool ignore_line_endings,
                             bool ignore_whitespace) {
    std::vector<diffy::Line> lines;

    uint32_t i = 1;
    std::string current;
    auto flush = [&]() {
        std::string sline = ignore_line_endings ? right_trim(current) : current;
        uint32_t hash = checksum_for(sline, ignore_whitespace);
        lines.push_back({i, hash, std::move(sline)});
        i++;
        current.clear();
    };

    for (char c : content) {
        current.push_back(c);
        if (c == '\n') {
            flush();
        }
    }
    // Trailing content with no final newline still counts as a line.
    if (!current.empty()) {
        flush();
    }

    return lines;
}