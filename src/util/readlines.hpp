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

    int indentation_level;
    int scope_level;

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

bool
parselines(const std::string& input_text, std::vector<Line>& lines, bool ignore_line_endings);

bool
readlines(const std::string& path, std::vector<Line>& lines, bool ignore_line_endings);

}  // namespace diffy
