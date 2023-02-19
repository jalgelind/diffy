#pragma once

#include "config_tokenizer.hpp"
#include "ordered_map.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace diffy {

/**

 Configuration language parser

 It's basically "INI with arrays, tables, strings, ints and bools". With better tokenization
 it can support floats, dates etc.

 The basic syntax is similar to simple TOML.

The parser works like this:

    Given the input

        [section]
            key = value

    We first tokenize the input text into these tokens:
        [, section, ], key, =, value

    This basically removes all whitespace and splits the input text into
    logical chunks; i.e a multi-line string is a single token, integers are
                        tokens and type-tagged as such.

    We then parse this stream of tokens and output a linear set of instructions
    that describes the configuration tree.

    In this case it would look something like this:

        TABLE_START 'section'
            KEY 'key'
            VALUE 'value'
        TABLE_END

    This can then be parsed into whatever.
*/

// Tree builder value type
enum class TbValueType {
    None,
    Int,
    Bool,
    String,
    Float,
};

// Tree builder operator
enum class TbOperator {
    Key,
    Value,
    ArrayStart,
    ArrayEnd,
    TableStart,
    TableEnd,
    Comment,
};

// Tree builder instruction
struct TbInstruction {
    static TbInstruction
    Comment(const std::string& comment) {
        TbInstruction ins;
        ins.op = TbOperator::Comment;
        ins.oparg_string = comment;
        return ins;
    }

    static TbInstruction
    ArrayStart() {
        TbInstruction ins;
        ins.op = TbOperator::ArrayStart;
        return ins;
    }

    static TbInstruction
    ArrayEnd() {
        TbInstruction ins;
        ins.op = TbOperator::ArrayEnd;
        return ins;
    }

    static TbInstruction
    TableStart() {
        TbInstruction ins;
        ins.op = TbOperator::TableStart;
        return ins;
    }

    static TbInstruction
    TableEnd() {
        TbInstruction ins;
        ins.op = TbOperator::TableEnd;
        return ins;
    }

    static TbInstruction
    Key(const std::string& key) {
        TbInstruction ins;
        ins.op = TbOperator::Key;
        ins.oparg_string = key;
        return ins;
    }

    static TbInstruction
    Value(const char* value) {
        TbInstruction ins;
        ins.op = TbOperator::Value;
        ins.oparg_type = TbValueType::String;
        ins.oparg_string = value;
        return ins;
    }

    static TbInstruction
    Value(const std::string& value) {
        TbInstruction ins;
        ins.op = TbOperator::Value;
        ins.oparg_type = TbValueType::String;
        ins.oparg_string = value;
        return ins;
    }

    static TbInstruction
    Value(const int value) {
        TbInstruction ins;
        ins.op = TbOperator::Value;
        ins.oparg_type = TbValueType::Int;
        ins.oparg_int = value;
        return ins;
    }

    static TbInstruction
    Value(const bool value) {
        TbInstruction ins;
        ins.op = TbOperator::Value;
        ins.oparg_type = TbValueType::Bool;
        ins.oparg_bool = value;
        return ins;
    }

    static TbInstruction
    Value(const float value) {
        TbInstruction ins;
        ins.op = TbOperator::Value;
        ins.oparg_type = TbValueType::Float;
        ins.oparg_float = value;
        return ins;
    }

    TbOperator op;
    TbValueType oparg_type = TbValueType::None;
    std::string oparg_string;
    int oparg_int = 0;
    bool oparg_bool = false;
    float oparg_float = 0.f;

    // HACK(ja): Extra flag used to determine comment context
    bool first_on_line = false;

    void
    set_first_on_line(bool first_on_line) {
        this->first_on_line = first_on_line;
    }

    bool
    get_first_on_line(bool first_on_line) const {
        return this->first_on_line;
    }

    bool
    operator==(const TbInstruction& other) {
        return op == other.op && oparg_type == other.oparg_type &&
               ((oparg_type == TbValueType::String && oparg_string == other.oparg_string) ||
                (oparg_type == TbValueType::Int && oparg_int == other.oparg_int) ||
                (oparg_type == TbValueType::Float &&
                 (std::abs(oparg_float - other.oparg_float) < 0.0000001)) ||
                (oparg_type == TbValueType::Bool && oparg_bool == other.oparg_bool) ||
                (oparg_type == TbValueType::None));
    }
};

// ---

struct Value {
    using Table = OrderedMap<std::string, Value>;
    using Array = std::vector<Value>;
    using Int = int32_t;
    using Float = float;
    using Bool = bool;
    using String = std::string;

    std::variant<Table, Array, Int, Float, Bool, String> v;

    // Comment lines attached to the value, or to the key it's assigned to.
    std::vector<std::string> value_comments;
    std::vector<std::string> key_comments;

    Value&
    operator[](std::string key) {
        assert(is_table());
        return as_table()[key];
    }

    Value&
    operator[](std::size_t index) {
        assert(is_array());
        return as_array()[index];
    }

    bool
    contains(const std::string& key) {
        if (is_table()) {
            return as_table().contains(key);
        }
        return false;
    }

    // Find a nested value using e.g. {"general", "font_size"}
    std::optional<std::reference_wrapper<Value>>
    lookup_value_by_path(std::initializer_list<std::string> path_components);

    // Find a nested value using e.g. "general.font_size"
    std::optional<std::reference_wrapper<Value>>
    lookup_value_by_path(const std::string_view dotted_path);

    // Sets a nested value using e.g. set("general.font_size", 16)
    bool
    set_value_at(const std::string_view dotted_path, Value value);

    // clang-format off
    bool is_array() { return std::holds_alternative<Value::Array>(v); }
    bool is_table() { return std::holds_alternative<Value::Table>(v); }
    bool is_int() { return std::holds_alternative<Value::Int>(v); }
    bool is_float() { return std::holds_alternative<Value::Float>(v); }
    bool is_bool() { return std::holds_alternative<Value::Bool>(v); }
    bool is_string() { return std::holds_alternative<Value::String>(v); }

    Array& as_array() { return std::get<Value::Array>(v); }
    Table& as_table() { return std::get<Value::Table>(v); }
    Int& as_int() { return std::get<Value::Int>(v); }
    Float& as_float() { return std::get<Value::Float>(v); }
    Bool& as_bool() { return std::get<Value::Bool>(v); }
    String& as_string() { return std::get<Value::String>(v); }
    // clang-format on
};

std::string
repr(Value& v);

// clang-format off
enum class ParseErrorKind {
    None         = 1 << 0,
    File         = 1 << 1, // File related, could be made more granular
    Tokenization = 1 << 2,
    Parsing      = 1 << 3,
    Other        = 1 << 4,
};
// clang-format on

struct ParseResult {
    ParseErrorKind kind;
    std::string error;

    bool
    is_ok() const {
        return kind == ParseErrorKind::None;
    }

    void
    set_error(config_tokenizer::Token& token, std::string error_message);
};

bool
cfg_parse(const std::string& input_data, ParseResult& result, std::function<void(TbInstruction)> emit_cb);

bool
cfg_parse_value_tree(const std::string& parser_input_data, ParseResult& result, Value& result_obj);

bool
cfg_parse_collect(const std::string& input_data,
                  ParseResult& result,
                  std::vector<TbInstruction>& instructions);

std::string
repr(TbOperator s);

std::string
repr(TbValueType vt);

}  // namespace diffy