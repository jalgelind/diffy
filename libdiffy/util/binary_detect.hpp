#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace diffy {

// Heuristic: a buffer "looks binary" if a NUL byte appears within the first
// `sample` bytes (8 KB, matching git's own check). Cheap, and matches how git
// and most pagers decide. The single source of truth for this decision — the
// CLI and GUI both route through it — so "is this binary?" can't drift between
// frontends. Used to suppress syntax highlighting and to route into the hex diff.
bool
looks_binary(std::string_view data, std::size_t sample = 8192);

bool
looks_binary(const std::vector<uint8_t>& data, std::size_t sample = 8192);

}  // namespace diffy
