#pragma once

#include <cstdint>

namespace diffy {

const uint16_t TermColorSupport_None      = 0;
const uint16_t TermColorSupport_Ansi4bit  = 1;  // 16 color palette
const uint16_t TermColorSupport_Ansi8bit  = 2;  // 256 color palette (216 24bit colors + 16 ansi + 24 gray)
const uint16_t TermColorSupport_Ansi24bit = 4;  // 24 bit true color

void
tty_get_term_size(int* rows, int* cols);

uint16_t
tty_get_capabilities();

}  // namespace diffy
