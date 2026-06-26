#pragma once

// TODO: MappedFile with a get_lines that returns gsl::string_span's?

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

struct Line {
    uint32_t line_number;
    uint32_t checksum;
    std::string line;

    uint32_t
    hash() const {
        return checksum;
    }

    bool
    operator<(const Line& other) const {
        return checksum < other.checksum;
    }

    bool
    operator==(const Line& other) const {
        return checksum == other.checksum;
    }
};

std::vector<Line>
readlines(const std::string& path, bool ignore_line_endings);

// Split an in-memory buffer into Lines using the same rules as readlines():
// each line keeps its trailing '\n' (a final line without one is kept as-is).
// Used by frontends that already hold the content (e.g. the GUI reads git blobs
// into memory rather than from a path).
std::vector<Line>
readlines_from_string(const std::string& content, bool ignore_line_endings);

}  // namespace diffy
