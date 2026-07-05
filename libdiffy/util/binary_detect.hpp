#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace diffy {

// Heuristic: a buffer "looks binary" if a NUL byte appears within the first
// `sample` bytes. Cheap, and matches how git and most pagers decide. Used both
// to suppress syntax highlighting and to route input into the hex diff.
bool
looks_binary(std::string_view data, std::size_t sample = 1024);

bool
looks_binary(const std::vector<uint8_t>& data, std::size_t sample = 1024);

}  // namespace diffy
