#pragma once

namespace diffy {

// TODO: Rename this to something suitable for both this, and for
//       Theme::compatible_with.
enum class TerminalColorCapability {
    None = 0,
    Ansi4bit,   // 16 color codes
    Ansi8bit,   // 256 color palette (216 24bit colors + 16 ansi + 24 gray)
    Ansi24bit,  // 24 bit true color
};

void
get_term_size(int* rows, int* cols);

TerminalColorCapability
get_terminal_color_capability();

}  // namespace diffy
