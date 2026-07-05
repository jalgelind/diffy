#include "config_serializer.hpp"

#include "config_tokenizer.hpp"

#include <fmt/format.h>

using namespace diffy;

namespace internal {

// Emit a single line of a string value: a raw 'single'-quoted literal when it
// needs no escaping (keeps existing configs byte-identical), otherwise an escaped
// "double"-quoted string. A literal can't contain a single-quote or carriage
// return (the caller has already split on newlines).
static std::string
quote_line(const std::string& line) {
    bool needs_escape = false;
    for (char c : line) {
        if (c == '\'' || c == '\r') {
            needs_escape = true;
            break;
        }
    }
    if (!needs_escape) {
        return "'" + line + "'";
    }
    return "\"" + diffy::config_tokenizer::escape_string(line) + "\"";
}

// Emit a string value. A single-line value is one quoted literal. A value with
// newlines is written as one quoted literal per line, each continuation aligned
// under the first line's opening quote (at column `align_col`) and joined by real
// newlines — the parser concatenates such adjacent literals back with '\n'.
static std::string
serialize_string(const std::string& s, std::size_t align_col) {
    if (s.find('\n') == std::string::npos) {
        return quote_line(s);
    }
    const std::string pad(align_col, ' ');
    std::string out;
    std::size_t start = 0;
    bool first = true;
    while (true) {
        auto nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!first) {
            out += "\n" + pad;
        }
        out += quote_line(line);
        first = false;
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

// Current column in `output` (chars since the last newline) — where the next
// emitted token will start.
static std::size_t
current_column(const std::string& output) {
    auto nl = output.rfind('\n');
    return output.size() - (nl == std::string::npos ? 0 : nl + 1);
}

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
        output += serialize_string(value.as_string(), current_column(output));
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