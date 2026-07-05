#include "util/binary_detect.hpp"

#include <algorithm>
#include <cstring>

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

}  // namespace diffy
