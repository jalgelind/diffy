#pragma once

#include "config_tokenizer.hpp"
#include "ordered_map.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
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
    TbOperator op;
    std::string oparg1;
    TbValueType oparg2_type = TbValueType::None;
    int oparg2_int = 0;
    bool oparg2_bool = false;

    // HACK(ja): Extra flag used to determine comment context
    bool first_on_line = false;

    bool
    operator==(const TbInstruction& other) {
        return op == other.op && oparg1 == other.oparg1 && oparg2_type == other.oparg2_type &&
               oparg2_int == other.oparg2_int && oparg2_bool == other.oparg2_bool;
    }
};

// ---

struct Value {
    using Table = OrderedMap<std::string, Value>;
    using Array = std::vector<Value>;
    using Int = int32_t;
    using Bool = bool;
    using String = std::string;

    std::variant<Table, Array, Int, Bool, String> v;

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

    // TODO: can we utilize this? We often just replace the whole value.
    //       but with this we can retain comments
    void
    swap_inner_value(Value&& other) {
        v.swap(other.v);
    }

    // clang-format off
    bool is_array() { return std::holds_alternative<Value::Array>(v); }
    bool is_table() { return std::holds_alternative<Value::Table>(v); }
    bool is_int() { return std::holds_alternative<Value::Int>(v); }
    bool is_bool() { return std::holds_alternative<Value::Bool>(v); }
    bool is_string() { return std::holds_alternative<Value::String>(v); }
    
    Array& as_array() { return std::get<Value::Array>(v); }
    Table& as_table() { return std::get<Value::Table>(v); }
    Int& as_int() { return std::get<Value::Int>(v); }
    Bool& as_bool() { return std::get<Value::Bool>(v); }
    String& as_string() { return std::get<Value::String>(v); }
    // clang-format on
};

// TODO: maybe all these should be members for consistency.

bool
cfg_value_contains(Value& v, const std::string& key);

bool
cfg_value_is_array(Value& v);

bool
cfg_value_is_table(Value& v);

bool
cfg_value_is_int(Value& v);

bool
cfg_value_is_bool(Value& v);

bool
cfg_value_is_string(Value& v);

Value::Array&
cfg_value_as_array(Value& v);

Value::Table&
cfg_value_as_table(Value& v);

Value::Int&
cfg_value_as_int(Value& v);

Value::Bool&
cfg_value_as_bool(Value& v);

Value::String&
cfg_value_as_string(Value& v);

std::string
repr(Value& v);

// clang-format off
enum class ParseErrorKind {
    None         = 1 << 0,
    File         = 1 << 1, // File related, kinda catch-all for now.
                           // TODO: check file permissions and file presence separately
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
    set_error(tok2::Token& token, std::string error_message);
};

bool
cfg_parse(const std::string& input_data, ParseResult& result, std::function<void(TbInstruction)> emit_cb);

bool
cfg_parse_value_tree(const std::string& parser_input_data, ParseResult& result, Value& result_obj);

bool
cfg_parse_collect(const std::string& input_data,
                  ParseResult& result,
                  std::vector<TbInstruction>& instructions);

}  // namespace diffy