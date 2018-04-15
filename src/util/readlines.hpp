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
        return checksum < other.checksum;  // TODO: wut. I guess it's for std::map
    }

    bool
    operator==(const Line& other) const {
        return checksum == other.checksum;
    }
};

std::vector<Line>
readlines(std::string path, bool ignore_line_endings);

}  // namespace diffy
