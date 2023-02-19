#include "config_serializer.hpp"

#include <fmt/format.h>

using namespace diffy;

namespace internal {

static std::string
indent(int count) {
    if (count < 0) {
        assert(false && "invalid count!");
        return "";
    }
    return std::string(count * 4, ' ');
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
                // output += "\n";
                output += "\n";
            });
            output += "\n";
        });
        return;
    }
}
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