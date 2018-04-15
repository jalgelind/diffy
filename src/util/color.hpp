#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

const uint32_t kStyleFlag_BOLD = 1 << 1;
const uint32_t kStyleFlag_DIM = 1 << 2;
const uint32_t kStyleFlag_ITALIC = 1 << 3;
const uint32_t kStyleFlag_UNDERLINE = 1 << 4;
const uint32_t kStyleFlag_BLINK = 1 << 5;  // really?
const uint32_t kStyleFlag_INVERSE = 1 << 7;
const uint32_t kStyleFlag_HIDDEN = 1 << 8;
const uint32_t kStyleFlag_STRIKETHROUGH = 1 << 9;

uint32_t
color_lookup_attributes(const std::vector<std::string>& attributes);

// 16 color palette
bool
translate_color_name_to_16pal_escape_sequence(const std::string& fg_color,
                                              const std::string& bg_color,
                                              uint32_t flags,
                                              std::string& result);

#if 0
// 256 color palette
bool
translate_color_name_to_256pal_escape_sequence(const std::string& fg_color,
                                               const std::string& bg_color,
                                               std::string& result);
// truetype color palette
// Only accepts a 24 bit hex value, #rrggbb
bool
translate_color_name_to_tt_escape_sequence(const std::string& fg_color,
                                           const std::string& bg_color,
                                           std::string& result);
#endif

void
dump_colors();

}  // namespace diffy
