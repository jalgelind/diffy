#include "util/hash.hpp"

#include <crc32c/crc32c.h>

uint32_t
hash::hash(const char* input, std::size_t len) {
    return crc32c::Crc32c(input, len);
}

uint32_t
hash::hash(const uint8_t* input, std::size_t len) {
    return crc32c::Crc32c(input, len);
}
