#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

// Read a whole file into memory as raw bytes. The binary/hex diff path uses this
// instead of readlines() so it never splits on '\n'. Returns false on any I/O
// error (out is left empty).
bool
read_file_bytes(const std::string& path, std::vector<uint8_t>& out);

}  // namespace diffy
