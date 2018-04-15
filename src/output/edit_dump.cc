#include "edit_dump.hpp"

#include <fmt/format.h>

using namespace diffy;

void
diffy::dump_diff_edits(const DiffInput<diffy::Line>& diff_input, const DiffResult& result) {
    for (auto e : result.edit_sequence) {
        std::string text;
        if (e.type == EditType::Insert)
            text = diff_input.B[static_cast<int>(e.b_index)].line;
        else
            text = diff_input.A[static_cast<int>(e.a_index)].line;
        std::string op = "";
        if (e.type == EditType::Insert)
            op = "+";
        else if (e.type == EditType::Delete)
            op = "-";
        else if (e.type == EditType::Meta)
            op = '!';

        if (text[text.size() - 1] != '\n')
            text += "\n";

        fmt::print("{:2} {:4}\t{:4}\t{}", op, e.a_index, e.b_index, text);
    }
}
