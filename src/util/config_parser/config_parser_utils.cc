#include "config_parser_utils.hpp"

#include "config_parser.hpp"

#include <fmt/format.h>

#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace diffy;

namespace internal {

static std::string
indent(int count) {
    if (count < 0) {
        assert(false && "Unbalanced scopes!");
        return "";
    }
    return std::string(count * 4, ' ');
}

void
dump_value_object(Value& v, std::string& s, int depth) {
    auto INDENT = [](int x) {
        return std::string((x) *2, ' ');
    };
    
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

void
serialize_obj(Value& value, int depth, std::string& output, bool is_last_element, bool parent_is_container) {
    auto is_on_empty_line = [](const std::string& s) {
        auto line_start = s.rfind('\n');
        if (line_start == std::string::npos) {
            return false;
        }
        for (auto i = line_start; i < s.size(); i++) {
            if (!config_tokenizer::is_whitespace(s[i])) {
                return false;
            }
        }
        return true;
    };

    int row = 0;
    int rows = 0;
    if (value.is_table() || value.is_array()) {
        if (value.is_array()) {
            auto& array = value.as_array();
            rows = array.size();
            if (rows == 0) {
                output += "[]";
            } else {
                output += "[\n" + indent(depth);
                for (auto& v : array) {
                    for (auto& comment : v.key_comments) {
                        if (!is_on_empty_line(output)) {
                            output += "\n" + indent(depth + 1);
                        }
                        output += indent(depth) + comment + "\n";
                        output += indent(depth);
                    }
                    serialize_obj(v, depth + 1, output, row == rows - 1, true);
                    row++;
                }
                output += "\n" + indent(depth - 1) + "]";
            }
        } else if (value.is_table()) {
            auto& table = value.as_table();
            rows = table.size();
            if (rows == 0) {
                output += "{}";
            } else {
                output += "{";
                for (auto& comment : value.value_comments) {
                    output += " " + comment + "\n";
                }
                output += "\n" + indent(depth);
                table.for_each([&](auto k, auto& v) {
                    bool last_iteration = row == rows - 1;
                    for (auto& comment : v.key_comments) {
                        if (!is_on_empty_line(output)) {
                            output += "\n" + indent(depth) + "\n";
                        }
                        output += comment + "\n";
                        output += indent(depth);
                    }
                    output += k + " = ";
                    serialize_obj(v, depth + 1, output, last_iteration, true);
                    row++;
                });
                output += "\n" + indent(depth - 1) + "}";
            }
        }

        if (!is_last_element) {
            if (parent_is_container) {
                output += ", \n" + indent(depth - 1);
            } else {
                output += ", ";
            }
        }

    } else if (value.is_int()) {
        output += fmt::format("{}", value.as_int());
        if (!is_last_element)
            output += ", ";
        for (auto& comment : value.value_comments) {
            output += " " + comment + "\n";
        }
    } else if (value.is_bool()) {
        output += fmt::format("{}", value.as_bool());
        if (!is_last_element)
            output += ", ";
        for (auto& comment : value.value_comments) {
            output += " " + comment + "\n";
        }
    } else if (value.is_string()) {
        output += fmt::format("'{}'", value.as_string());
        if (!is_last_element)
            output += ", ";
        for (auto& comment : value.value_comments) {
            output += " " + comment + "\n";
        }
    }
}

void
serialize_section(Value& value, int depth, std::string& output) {
    // Write sections at the lowest depth.
    // A section is a table of tables
    for (auto& comment : value.key_comments) {
        output += comment + "\n";
    }
    for (auto& comment : value.value_comments) {
        output += comment + "\n";
    }
    if (depth == 0 && value.is_table()) {
        value.as_table().for_each([&](auto k, auto& v) {
            for (auto& comment : v.key_comments) {
                output += indent(depth) + comment + "\n";
            }
            output += "[" + k + "]";
            for (auto& comment : v.value_comments) {
                output += " " + comment + "\n";
            }
            // tweak depth for indentation of section keys
            output += "\n";
            assert(v.is_table());
            v.as_table().for_each([&](auto k1, auto& v1) {
                for (auto& comment : v1.key_comments) {
                    output += indent(depth) + comment + "\n";
                }
                output += indent(depth) + k1 + " = ";
                serialize_obj(v1, depth + 1, output, true, true);
                //output += "\n";
                output += "\n";
            });
            output += "\n";
        });
        return;
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
    std::ifstream ifs;
    ifs.open(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        result.kind = ParseErrorKind::File;
        result.error = "Failed to open file for reading";
        return false;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();

    return cfg_parse(buffer.str(), result, consume_instruction);
}

std::string
diffy::cfg_serialize_obj(Value& value) {
    std::string output;
    internal::serialize_obj(value, 1, output, true, false);
    return output;
}

std::string
diffy::cfg_serialize(Value& value, int depth) {
    std::string output;
    internal::serialize_section(value, depth, output);
    return output;
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

std::string
diffy::repr(TbValueType vt) {
    std::string result = "";
    std::vector<std::tuple<TbValueType, std::string>> lut = {
        {TbValueType::Bool, "Bool"},
        {TbValueType::Int, "Int"},
        {TbValueType::String, "String"},
        {TbValueType::None, "None"},
    };

    for (const auto& [op, value] : lut) {
        if (op == vt) {
            return "\033[1;36m" + value + "\033[0m";
        }
    }

    assert(false && "unknown value type");
    return "";
}

std::string
diffy::repr(TbOperator s) {
    std::string result = "";
    std::vector<std::tuple<diffy::TbOperator, std::string>> lut = {
        {diffy::TbOperator::Key, "Key"},
        {diffy::TbOperator::Value, "Value"},
        {diffy::TbOperator::ArrayStart, "ArrayStart"},
        {diffy::TbOperator::ArrayEnd, "ArrayEnd"},
        {diffy::TbOperator::TableStart, "TableStart"},
        {diffy::TbOperator::TableEnd, "TableEnd"},
        {diffy::TbOperator::Comment, "Comment"},
    };

    for (const auto& [state, value] : lut) {
        if (state == s) {
            return "\033[1;34m" + value + "\033[0m";
        }
    }

    assert(false && "missing implementation of added enum");
    return "";
}