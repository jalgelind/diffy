#include "config_parser_utils.hpp"

#include "config_parser.hpp"

#include <fmt/format.h>

#include <fstream>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace diffy;

namespace internal {

void
dump_value_object(Value& v, std::string& s, int depth) {
    auto INDENT = [](int x) { return std::string((x) *2, ' '); };

    s += fmt::format("{}", INDENT(depth));
    s += fmt::format("Comments:\n");
    for (auto& comment : v.key_comments) {
        s += fmt::format("{}", INDENT(depth + 2));
        s += fmt::format("K: {}\n", comment);
    }
    for (auto& comment : v.value_comments) {
        s += fmt::format("{}", INDENT(depth + 2));
        s += fmt::format("V: {}\n", comment);
    }

    s += fmt::format("{}", INDENT(depth));
    s += fmt::format("Value:\n");

    if (v.is_table()) {
        auto& table = v.as_table();
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("Table {{\n");
        table.for_each([depth, &s, INDENT](const std::string& key, Value& value) {
            s += fmt::format("{}", INDENT(depth + 2));
            s += fmt::format("Key: '{}'\n", key);
            dump_value_object(value, s, depth + 3);
            s += fmt::format("\n");
        });
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("}}\n");
    } else if (v.is_array()) {
        auto& array = v.as_array();
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("Array [\n");
        for (auto& value : array) {
            dump_value_object(value, s, depth + 2);
            s += fmt::format("\n");
        }
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("]\n");

    } else if (v.is_int()) {
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("{}\n", v.as_int());
    } else if (v.is_bool()) {
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("{}\n", v.as_bool());
    } else if (v.is_string()) {
        s += fmt::format("{}", INDENT(depth + 1));
        s += fmt::format("'{}'\n", v.as_string());
    } else {
        assert(false && "bad state");
    }
}

}  // namespace internal

bool
diffy::cfg_load_file(const std::string& file_path, ParseResult& result, Value& result_obj) {
    std::ifstream ifs;
    ifs.open(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        result.kind = ParseErrorKind::File;
        result.error = "Failed to open file for reading";
        return false;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();

    return cfg_parse_value_tree(buffer.str(), result, result_obj);
}

bool
cfg_load_file(const std::string& file_path,
              ParseResult& result,
              std::function<void(TbInstruction)> consume_instruction) {
    if (!std::filesystem::exists(file_path)) {
        result.kind = ParseErrorKind::File;
        result.error = "File does not exist";
        return false;
    }

    auto status = std::filesystem::status(file_path);
    if (!std::filesystem::is_regular_file(status)) {
        result.kind = ParseErrorKind::File;
        result.error = "File is not a regular file";
        return false;
    }
    if ((status.permissions() & std::filesystem::perms::owner_read) != std::filesystem::perms::owner_read) {
        result.kind = ParseErrorKind::File;
        result.error = "File does not have read permission";
        return false;
    }

    std::ifstream ifs;
    try {
        ifs.open(file_path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            result.kind = ParseErrorKind::File;
            result.error = "Failed to open file for reading";
            return false;
        }

        std::stringstream buffer;
        buffer << ifs.rdbuf();

        if (buffer.str().empty()) {
            result.kind = ParseErrorKind::File;
            result.error = "File is empty";
            return false;
        }

        return cfg_parse(buffer.str(), result, consume_instruction);
    } catch (std::exception& e) {
        result.kind = ParseErrorKind::File;
        result.error = "Failed to load file: " + std::string(e.what());
        return false;
    }
}

//
// Debug / formatting utilities
//

bool
diffy::cfg_parse_dump(const std::string& input_data, diffy::ParseResult& result) {
    std::vector<TbInstruction> instructions;
    if (!cfg_parse(input_data, result, [&](auto x) { instructions.push_back(x); })) {
        return false;
    }
    cfg_dump_instructions(instructions);

    return result.is_ok();
}

void
diffy::cfg_dump_instructions(std::vector<TbInstruction>& inst) {
    int depth = 0;
    int ins_idx = 0;
    for (auto& ins : inst) {
        if (ins.op == TbOperator::ArrayEnd || ins.op == TbOperator::TableEnd)
            depth--;
        std::string indent;
        for (int i = 0; i < depth; i++)
            indent += "    ";
        auto vt = ins.oparg_type == TbValueType::None ? "" : repr(ins.oparg_type);
        fmt::print("{:>3} |{:>2}{} {} {} {}", ins_idx, depth, indent, repr(ins.op), ins.oparg_string, vt);
        switch (ins.oparg_type) {
            case TbValueType::Bool: {
                fmt::print(" {}\n", ins.oparg_bool);
            } break;
            case TbValueType::Int: {
                fmt::print(" {}\n", ins.oparg_int);
            } break;
            default: {
                fmt::print("\n");
            }
        }
        ins_idx++;
        if (ins.op == TbOperator::ArrayStart || ins.op == TbOperator::TableStart)
            depth++;
    }
}

std::string
diffy::cfg_dump_value_object(Value& v, int depth) {
    std::string result;
    internal::dump_value_object(v, result, 0);
    return result;
}