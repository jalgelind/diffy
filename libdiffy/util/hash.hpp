#pragma once

#include <cstddef>
#include <cstdint>

namespace hash {

uint32_t
hash(const char* input, std::size_t len);
uint32_t
hash(const uint8_t* input, std::size_t len);

}  // namespace hash