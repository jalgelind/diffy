#include "readlines.hpp"

#include "util/hash.hpp"

#include <string>
#include <vector>

#include <errno.h>
#include <limits.h>

#include <cstdlib>

using ssize_t = long;

namespace internal {

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
    return internal::getdelim(lineptr, n, '\n', stream);
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

    char* line = nullptr;
    size_t len = 0;
    ssize_t nread;

    FILE* stream = fopen(path.c_str(), "rb");
    if (!stream)
        return lines;  // TODO: Error handling

    uint32_t i = 1;
    while ((nread = internal::getline(&line, &len, stream)) != -1) {
        std::string sline(line);
        if (ignore_line_endings) {
            sline = internal::right_trim(sline);
        }
        uint32_t hash = hash::hash(sline.c_str(), static_cast<uint32_t>(sline.size()));
        lines.push_back({i, hash, std::move(sline)});
        i++;
    }

    free(line);
    fclose(stream);

    return lines;
}