#include "text_layout.hpp"

namespace diffy::gui {

std::string
expand_for_display(const std::string& in, int tab_width, int& col) {
    std::string out;
    out.reserve(in.size());
    if (tab_width < 1) {
        tab_width = 1;
    }
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '\t') {
            const int n = tab_width - (col % tab_width);
            out.append(static_cast<size_t>(n), ' ');
            col += n;
            ++i;
        } else if (c < 0x20 || c == 0x7f) {
            out.push_back(' ');
            ++col;
            ++i;
        } else {
            out.push_back(static_cast<char>(c));
            ++col;
            ++i;
            // Keep UTF-8 continuation bytes with the lead byte; they don't add a
            // display column.
            while (i < in.size() && (static_cast<unsigned char>(in[i]) & 0xc0) == 0x80) {
                out.push_back(in[i]);
                ++i;
            }
        }
    }
    return out;
}

}  // namespace diffy::gui
