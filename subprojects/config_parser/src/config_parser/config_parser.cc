#include "config_parser.hpp"

#include "config_parser_utils.hpp"
#include "config_tokenizer.hpp"

#include <fmt/format.h>

#include <cassert>
#include <functional>
#include <optional>
#include <stack>
#include <tuple>
#include <variant>

#define TRACE_ENABLE 0
#define TRACE(...)               \
    if (TRACE_ENABLE) {          \
        fmt::print(__VA_ARGS__); \
    }

using namespace diffy;
using namespace config_tokenizer;

namespace internal {
std::tuple<std::string_view, std::string_view>
str_split2(const std::string_view s, char delimiter) {
    auto pos = s.find(delimiter);
    if (pos == std::string::npos) {
        return std::make_tuple(s, "");
    }

    return std::make_tuple(s.substr(0, pos), s.substr(pos + 1, std::string::npos));
}

bool
tokenize(const std::string& input_data,
         std::vector<config_tokenizer::Token>& tokens,
         diffy::ParseResult& result) {
    diffy::config_tokenizer::ParseOptions config_tokenizer_options;
    config_tokenizer_options.strip_newlines = true;
    config_tokenizer_options.strip_spaces = true;
    config_tokenizer_options.strip_quotes = true;                   // drop "'" and '"'
    config_tokenizer_options.strip_annotated_string_tokens = true;  // drop '#' and '//' from comments
    config_tokenizer_options.strip_comments = false;
    config_tokenizer_options.append_terminator =
        true;  // Append termination token to avoid some bounds checking

    diffy::config_tokenizer::ParseResult config_tokenizer_result;
    if (!diffy::config_tokenizer::tokenize(input_data, config_tokenizer_options, config_tokenizer_result)) {
        result.kind = ParseErrorKind::Tokenization;
        result.error = config_tokenizer_result.error;
        return false;
    }

    // All good!
    tokens = config_tokenizer_result.tokens;
    result.kind = ParseErrorKind::None;
    return true;
}
}  // namespace internal

std::optional<std::reference_wrapper<Value>>
Value::lookup_value_by_path(std::initializer_list<std::string> path_components) {
    std::deque<std::string> components{path_components};
    std::string key;
    Value* result_value = this;
    while (!components.empty()) {
        key = components.front();
        if (result_value->contains(key)) {
            result_value = &(*result_value)[key];
            components.pop_front();
        } else {
            break;
        }
    }
    if (components.empty()) {
        return std::reference_wrapper(*result_value);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<Value>>
Value::lookup_value_by_path(const std::string_view dotted_path) {
    Value* result_value = this;
    bool done = false;
    std::string_view remaining{dotted_path};
    while (!done) {
        auto [head, rest] = internal::str_split2(remaining, '.');
        std::string key{head};
        if (result_value->contains(key)) {
            result_value = &(*result_value)[key];
            remaining = rest;
        } else {
            break;
        }
    }
    if (remaining.empty()) {
        return std::reference_wrapper(*result_value);
    }
    return std::nullopt;
}

bool
Value::set_value_at(const std::string_view dotted_path, Value value) {
    Value* iter = this;
    bool done = false;
    std::string_view remaining{dotted_path};
    while (!done) {
        auto [head, rest] = internal::str_split2(remaining, '.');
        std::string key{head};
        bool is_at_insert_node = rest.empty();

        if (!is_at_insert_node) {
            if (!iter->contains(std::string{key})) {
                iter->as_table().insert(key, {Value::Table{}});
            }
            remaining = rest;
            iter = &(*iter)[key];
            continue;
        }

        (*iter)[std::string(key)] = value;
        return true;
    }
    return false;
}

// Our state machine states.
// Note that we have a few entry points. If we want to parse a complete
// file with sections; we start at `ParseSection`. For parsing values, we
// start at `ParseObject`.
//
// There might be a few redundant states - but the current set is fairly easy
// to troubleshoot. Set `TRACE_ENABLE` to dump the parser states to stdout.
//
enum class State {
    ParseSection,

    ParseKey,
    ParseObject,

    ParseTableStart,
    ParseTableItems,
    ParseTableEnd,

    ParseArrayStart,
    ParseArrayValues,
    ParseArrayEnd,

    ParseValue,

    PopScope,

    Finish,
};

std::string
repr(State s);

enum class Scope {
    Section,
    Table,
    Array,
};

std::string
repr(Scope s);

void
diffy::ParseResult::set_error(config_tokenizer::Token& token, std::string error_message) {
    this->kind = ParseErrorKind::Parsing;
    this->error = fmt::format("'{}' at line {} column {}", error_message, token.line, token.column);
}

bool
diffy::cfg_parse(const std::string& input_data,
                 diffy::ParseResult& result,
                 std::function<void(TbInstruction)> emit_cb) {
    std::vector<config_tokenizer::Token> input_tokens;
    if (!internal::tokenize(input_data, input_tokens, result)) {
        return false;
    }

    //
    // State machine "DSL"
    //
    // Internal state:
    //  input_tokens | vector of all tokens
    //        cursor | current index into `tokens`
    //         state | current parsing state
    //   scope_stack | stack of states to handle nested values
    //
    // Local state:
    //         token | token reference into `input_tokens[cursor]`
    //

    std::size_t cursor = 0;

    // clang-format off
    #define PARSER_NEXT_STATE() {                   \
        continue;                                   \
    }

    #define PARSER_NEXT_TOKEN() {                   \
        cursor++;                                   \
        token = input_tokens[cursor];               \
        if (token.id & config_tokenizer::TokenId_Terminator && !in_critical_section) { \
            state = State::Finish;                  \
            PARSER_NEXT_STATE();                    \
        }                                           \
    }

    #define PARSER_EXPECT(expected_id) {                                                                         \
        if (!((expected_id) & token.id)) {                                                                       \
            result.set_error(token, fmt::format("Expected {}, found {} [src:{}]",                                \
                diffy::config_tokenizer::repr(expected_id), diffy::config_tokenizer::repr(token.id), __LINE__)); \
            return false;                                                                                        \
        }                                                                                                        \
    }

    #define PARSER_EXPECT_AND_ADVANCE(expected_id) {                                                             \
        if (!((expected_id) & token.id)) {                                                                       \
            result.set_error(token, fmt::format("Expected {}, found {} [src:{}]",                                \
                diffy::config_tokenizer::repr(expected_id), diffy::config_tokenizer::repr(token.id), __LINE__)); \
            return false;                                                                                        \
        } else {                                                                                                 \
            PARSER_NEXT_TOKEN();                                                                                 \
        }                                                                                                        \
    }

    #define PARSER_GIVE_UP(message) {                                                                  \
        if (token.id & config_tokenizer::TokenId_Terminator)                                           \
            PARSER_NEXT_STATE();                                                                       \
        result.set_error(token, fmt::format("error: \033[1m'{}'\033[0m while processing {} [src: {}]", \
                message, diffy::config_tokenizer::repr(token.id), __LINE__));                          \
        return false;                                                                                  \
    }

    #define PARSER_JUMP(next_state) { \
        state = next_state;           \
        PARSER_NEXT_STATE();          \
    }

    #define PARSER_TRANSITION_TO(token_id, next_state) { \
        if (token.id & (token_id)) {                     \
            state = next_state;                          \
            PARSER_NEXT_STATE();                         \
        }                                                \
    }

    #define PARSER_ADVANCE_AND_TRANSITION_TO(token_id, next_state) { \
        if (token.id & (token_id)) {                                 \
            state = next_state;                                      \
            PARSER_NEXT_TOKEN();                                     \
            PARSER_NEXT_STATE();                                     \
        }                                                            \
    }

    #define PARSER_CONSUME(expected_id, value_consumer) { \
        PARSER_EXPECT(expected_id);                       \
        value_consumer(token.str_from(input_data));       \
        PARSER_NEXT_TOKEN();                              \
    }

    #define PARSER_EAT_COMMENTS() {                                                  \
        while (token.id & config_tokenizer::TokenId_Comment) {                       \
            auto ins = TbInstruction::Comment(token.str_from(input_data));           \
            ins.set_first_on_line(token.id & config_tokenizer::TokenId_FirstOnLine); \
            emit_ins(ins);                                                           \
            PARSER_NEXT_TOKEN();                                                     \
        }                                                                            \
    }

    #define PARSER_SKIP(ids) {   \
        if (token.id & (ids)) {  \
            PARSER_NEXT_TOKEN(); \
        }                        \
    }
    // clang-format on

    auto emit_ins = [&](TbInstruction ins, bool first_on_line = false) {
        static int cnt = 0;
        cnt++;
        TRACE("* Emit[{}] {} {}\n", cnt, repr(ins.op), ins.oparg_string);
        ins.first_on_line = first_on_line;
        emit_cb(ins);
    };

    bool in_critical_section = false;

    // Scope stack
    std::stack<Scope> scope_stack;

    // Current state
    State state = State::ParseSection;

    //
    // Collect a few tokens to determine where to in the state machine we should jump.
    //
    {
        std::vector<Token> state_selection_tokens;
        for (const auto& token : input_tokens) {
            if (!(token.id & TokenId_Comment)) {
                state_selection_tokens.push_back(token);
            }
            if (state_selection_tokens.size() >= 3) {
                break;
            }
        }

        if (!state_selection_tokens.empty()) {
            // Figure out if we're in a section or not
            if (state_selection_tokens.size() >= 3 && state_selection_tokens[0].id & TokenId_OpenBracket &&
                state_selection_tokens[1].id & TokenId_Identifier &&
                state_selection_tokens[2].id & TokenId_CloseBracket) {
                state = State::ParseSection;
            } else {
                // Handle parsing values, i.e: "hello", 1, false, [1, 2] or { foo = "bar"}
                TokenId object_id = TokenId_OpenBracket | TokenId_OpenCurly | TokenId_MetaValue;
                if (state_selection_tokens[0].id & object_id) {
                    state = State::ParseObject;
                }

                // Maybe we're parsing something like `key = value`?
                if (state_selection_tokens.size() > 2 && state_selection_tokens[0].id & TokenId_Identifier &&
                    state_selection_tokens[1].id & TokenId_Assign) {
                    scope_stack.push(Scope::Table);
                    emit_ins(TbInstruction{TbOperator::TableStart});
                    state = State::ParseKey;
                }
            }
        }
    }

    bool done = false;
    while (!done) {
        if (cursor >= input_tokens.size() && scope_stack.empty()) {
            state = State::Finish;
        }

        Token& token = input_tokens[cursor];

        static int cnt = 0;
        cnt++;
        TRACE("\033[1m({: 2}\033[0m:[#{}]:{:10}\033[1m)\033[0m Token {} '{}'\n", cnt, scope_stack.size(),
              repr(state), config_tokenizer::repr(token.id), token.str_from(input_data));

        using namespace config_tokenizer;
        switch (state) {
            case State::ParseSection: {
                PARSER_EAT_COMMENTS();
                // Expect [
                PARSER_EXPECT_AND_ADVANCE(TokenId_OpenBracket);

                scope_stack.push(Scope::Section);
                TRACE("* Pushing stack (section) {}\n", repr(scope_stack.top()));

                PARSER_CONSUME(TokenId_Identifier,
                               ([&](const std::string& key) { emit_ins(TbInstruction::Key(key)); }));

                emit_ins(TbInstruction::TableStart());

                // Expect ]
                PARSER_EXPECT_AND_ADVANCE(TokenId_CloseBracket);

                PARSER_EAT_COMMENTS();

                // Section header followed by key identifier
                PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseKey);

                PARSER_GIVE_UP("Exhausted");
            } break;
            case State::ParseKey: {
                // Consume ´key´ in ´key = ...´
                PARSER_EXPECT(TokenId_Identifier);
                PARSER_CONSUME(TokenId_Identifier,
                               ([&](const std::string& key) { emit_ins(TbInstruction::Key(key)); }));

                PARSER_EAT_COMMENTS();

                PARSER_EXPECT_AND_ADVANCE(TokenId_Assign);

                PARSER_EAT_COMMENTS();

                PARSER_TRANSITION_TO(TokenId_MetaObject, State::ParseObject);

                PARSER_GIVE_UP("Exhausted");
            } break;
            case State::ParseObject: {
                PARSER_EAT_COMMENTS();
                PARSER_EXPECT(TokenId_MetaObject);

                PARSER_TRANSITION_TO(TokenId_OpenBracket, State::ParseArrayStart);
                PARSER_TRANSITION_TO(TokenId_OpenCurly, State::ParseTableStart);
                PARSER_TRANSITION_TO(TokenId_MetaValue, State::ParseValue);

                PARSER_GIVE_UP("Exhausted");
            } break;
            case State::ParseValue: {
                PARSER_CONSUME(TokenId_MetaValue, ([&](const std::string& value) {
                                   TbInstruction ins;
                                   if (token.id & TokenId_Boolean) {
                                       ins = TbInstruction::Value(token.token_boolean_arg);
                                   } else if (token.id & TokenId_Integer) {
                                       ins = TbInstruction::Value(token.token_int_arg);
                                   } else if (token.id & TokenId_Float) {
                                       ins = TbInstruction::Value(token.token_float_arg);
                                   } else if (token.id & TokenId_String) {
                                       ins = TbInstruction::Value(token.str_from(input_data));
                                   } else {
                                       assert(false && "Not reached");
                                       ins.oparg_type = TbValueType::String;
                                   }
                                   emit_ins(ins);
                               }));

                PARSER_EAT_COMMENTS();

                switch (scope_stack.top()) {
                    case Scope::Section: {
                        PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseKey);

                        emit_ins(TbInstruction::TableEnd());
                        TRACE("* Popping stack {}\n", repr(scope_stack.top()));
                        scope_stack.pop();

                        PARSER_TRANSITION_TO(TokenId_OpenBracket, State::ParseSection);
                        PARSER_GIVE_UP("Exhausted");
                    }
                    case Scope::Table: {
                        PARSER_ADVANCE_AND_TRANSITION_TO(TokenId_Comma, State::ParseTableItems);
                        PARSER_TRANSITION_TO(TokenId_CloseCurly, State::ParseTableEnd);
                        PARSER_GIVE_UP("Exhausted");
                    } break;
                    case Scope::Array: {
                        PARSER_ADVANCE_AND_TRANSITION_TO(TokenId_Comma, State::ParseObject);
                        PARSER_TRANSITION_TO(TokenId_CloseBracket, State::ParseArrayEnd);
                        PARSER_GIVE_UP("Exhausted");
                    } break;
                    default: {
                        PARSER_GIVE_UP("Unhandeled scope");
                    }
                };

            } break;
            case State::ParseTableStart: {
                PARSER_EXPECT_AND_ADVANCE(TokenId_OpenCurly);

                scope_stack.push(Scope::Table);
                TRACE("* Pushing stack {}\n", repr(scope_stack.top()));

                emit_ins(TbInstruction::TableStart());

                PARSER_EAT_COMMENTS();

                PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseTableItems);
                PARSER_TRANSITION_TO(TokenId_CloseCurly, State::ParseTableEnd);

                PARSER_GIVE_UP("Exhausted");
            } break;

            case State::ParseArrayStart: {
                PARSER_EXPECT_AND_ADVANCE(TokenId_OpenBracket);

                PARSER_EAT_COMMENTS();

                scope_stack.push(Scope::Array);
                TRACE("* Pushing stack {}\n", repr(scope_stack.top()));
                emit_ins(TbInstruction::ArrayStart());

                PARSER_TRANSITION_TO(TokenId_MetaObject, State::ParseArrayValues);
                PARSER_TRANSITION_TO(TokenId_CloseBracket, State::ParseArrayEnd);

                PARSER_GIVE_UP("Exhausted");
            } break;

            case State::ParseTableItems: {
                PARSER_EAT_COMMENTS();

                PARSER_TRANSITION_TO(TokenId_CloseCurly, State::ParseTableEnd);

                PARSER_EAT_COMMENTS();
                PARSER_SKIP(TokenId_Comma);
                PARSER_EAT_COMMENTS();

                // Consume ´key´ in ´key = ...´
                PARSER_EXPECT(TokenId_Identifier);
                PARSER_CONSUME(TokenId_Identifier,
                               ([&](const std::string& key) { emit_ins(TbInstruction::Key(key)); }));

                PARSER_EAT_COMMENTS();

                // Consume the equal sign in ´key = ...´
                PARSER_EXPECT_AND_ADVANCE(TokenId_Assign);

                PARSER_EAT_COMMENTS();

                // Handle key = {, key = [, key = "apa" etc
                PARSER_TRANSITION_TO(TokenId_OpenCurly, State::ParseObject);
                PARSER_TRANSITION_TO(TokenId_OpenBracket, State::ParseObject);
                PARSER_TRANSITION_TO(TokenId_MetaValue, State::ParseObject);

                PARSER_GIVE_UP("Exhausted");
            } break;
            case State::ParseArrayValues: {
                PARSER_EAT_COMMENTS();
                PARSER_SKIP(TokenId_Comma);
                PARSER_EAT_COMMENTS();
                PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseKey);
                PARSER_TRANSITION_TO(TokenId_CloseBracket, State::ParseArrayEnd);
                PARSER_JUMP(State::ParseObject);
                PARSER_GIVE_UP("Exhausted");
            } break;

            case State::ParseTableEnd: {
                PARSER_EXPECT(TokenId_CloseCurly);
                PARSER_EAT_COMMENTS();
                PARSER_JUMP(State::PopScope);
            } break;
            case State::ParseArrayEnd: {
                PARSER_EXPECT(TokenId_CloseBracket);
                PARSER_EAT_COMMENTS();
                PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseKey);
                PARSER_JUMP(State::PopScope);
            } break;

            case State::PopScope: {
                Scope child_scope = scope_stack.top();

                TRACE("* Popping stack {}\n", repr(child_scope));
                scope_stack.pop();

                in_critical_section = true;
                PARSER_SKIP(TokenId_CloseCurly | TokenId_CloseBracket);
                PARSER_EAT_COMMENTS();
                in_critical_section = false;

                TbOperator scope_close_op =
                    child_scope == Scope::Array ? TbOperator::ArrayEnd : TbOperator::TableEnd;
                emit_ins({scope_close_op});

                // While in the critical section, we may have parsed up until the final terminator token
                // so we should then finish up.
                PARSER_TRANSITION_TO(TokenId_Terminator, State::Finish);

                State next_state =
                    scope_stack.top() == Scope::Array ? State::ParseArrayValues : State::ParseTableItems;

                PARSER_EAT_COMMENTS();

                switch (scope_stack.top()) {
                    case Scope::Section: {
                        PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseKey);
                        emit_ins(TbInstruction::TableEnd());
                    } break;
                    case Scope::Table: {
                        PARSER_JUMP(next_state);
                    } break;
                    case Scope::Array: {
                        PARSER_JUMP(next_state);
                    } break;
                    default: {
                        PARSER_GIVE_UP("Unhandeled scope");
                    } break;
                };
            }
            case State::Finish: {
                while (!scope_stack.empty()) {
                    switch (scope_stack.top()) {
                        case Scope::Array: {
                            emit_ins(TbInstruction::ArrayEnd());
                        } break;
                        case Scope::Section:
                            // fall-through
                        case Scope::Table: {
                            emit_ins(TbInstruction::TableEnd());
                        } break;
                    };
                    scope_stack.pop();
                }
                done = true;
            } break;
            default:
                break;
        }
    }

    return true;
}

bool
diffy::cfg_parse_value_tree(const std::string& input_data, diffy::ParseResult& result, Value& root) {
    root = Value{Value::Table{}};
    std::stack<std::reference_wrapper<Value>> value_stack;
    std::string last_key;

    std::vector<std::string> comments;

    value_stack.push(std::ref(root));

    // This is the value we want to attach context to, e.g:
    // key = "my value" # I made that value myself!
    //
    // Other comments will be attached to the next seen key.
    Value* comment_context_value = &value_stack.top().get();

    // make value and collect comments
    auto make_value = [&comments](Value value) {
        value.key_comments = comments;
        comments.clear();
        return value;
    };

    auto stack_push = [&](Value& v) {
        value_stack.push(v);
        comment_context_value = (Value*) &value_stack.top().get();
    };

    auto update_tree = [&](TbInstruction ins) {
        switch (ins.op) {
            case TbOperator::Comment: {
                if (comment_context_value != nullptr) {
                    comment_context_value->value_comments.push_back(ins.oparg_string);
                    comment_context_value = nullptr;
                } else {
                    comments.push_back(ins.oparg_string);
                }
            } break;
            case TbOperator::TableStart: {
                // If we don't have a key, we're probably parsing a table value
                //       i.e "{ foo = 'bar' }".
                // Since we don't have a key, and we're provided with an empty table for
                // storing the upcoming key/value-pairs, we can just ignore this instruction.
                if (last_key.empty()) {
                    break;
                }

                auto& top_value = value_stack.top().get();
                if (top_value.is_array()) {
                    auto& array = top_value.as_array();
                    auto value = make_value(Value{Value::Table{}});
                    array.push_back(value);
                    stack_push(std::ref(array.back()));
                } else if (top_value.is_table()) {
                    auto& table = top_value.as_table();
                    auto value = make_value(Value{Value::Table{}});
                    table.insert(last_key, value);
                    stack_push(std::ref(table[last_key]));
                } else {
                    assert(0 && "What?!");
                }
                comment_context_value = &value_stack.top().get();

            } break;
            case TbOperator::Key: {
                last_key = ins.oparg_string;
                comment_context_value = nullptr;
            } break;
            case TbOperator::Value: {
                Value value;

                if (ins.oparg_type == TbValueType::Int) {
                    value = make_value(Value{Value::Int{ins.oparg_int}});
                } else if (ins.oparg_type == TbValueType::Bool) {
                    value = make_value(Value{Value::Bool{ins.oparg_bool}});
                } else if (ins.oparg_type == TbValueType::Float) {
                    value = make_value(Value{Value::Float{ins.oparg_float}});
                } else {
                    value = make_value(Value{Value::String{ins.oparg_string}});
                }

                auto& top_value = value_stack.top().get();
                if (top_value.is_array()) {
                    auto& array = top_value.as_array();
                    array.push_back(value);
                    if (!ins.first_on_line) {
                        comment_context_value = &array[array.size() - 1];
                    }
                } else if (top_value.is_table()) {
                    auto& table = top_value.as_table();
                    table.insert(last_key, value);
                    if (!ins.first_on_line) {
                        comment_context_value = &table[last_key];
                    }
                } else {
                    assert(0 && "What?!");
                }
            } break;
            case TbOperator::TableEnd: {
                if (!value_stack.empty()) {
                    value_stack.pop();
                }
                comment_context_value = nullptr;
            } break;
            case TbOperator::ArrayStart: {
                assert(!last_key.empty() && "Unknown key");

                auto& top_value = value_stack.top().get();
                if (top_value.is_array()) {
                    auto& array = top_value.as_array();
                    auto value = make_value(Value{Value::Array{}});
                    array.push_back({value});
                    stack_push(std::ref(array.back()));
                } else if (top_value.is_table()) {
                    auto& table = top_value.as_table();
                    auto value = make_value(Value{Value::Array{}});
                    table.insert(last_key, value);
                    stack_push(std::ref(table[last_key]));
                } else {
                    assert(0 && "What?!");
                }
                comment_context_value = &value_stack.top().get();
            } break;
            case TbOperator::ArrayEnd: {
                if (!value_stack.empty()) {
                    value_stack.pop();
                }
                comment_context_value = nullptr;
            } break;
            default: {
                assert(0 && "unhandled operator");
            }
        }
    };
    return cfg_parse(input_data, result, update_tree);
}

bool
diffy::cfg_parse_collect(const std::string& input_data,
                         diffy::ParseResult& result,
                         std::vector<TbInstruction>& instructions) {
    return cfg_parse(input_data, result, [&](auto x) { instructions.push_back(x); });
}

std::string
repr(State s) {
    std::string result = "";
    std::vector<std::tuple<State, std::string>> lut = {{State::ParseSection, "ParseSection"},
                                                       {State::ParseKey, "ParseKey"},
                                                       {State::ParseObject, "ParseObject"},
                                                       {State::ParseTableStart, "ParseTableStart"},
                                                       {State::ParseTableItems, "ParseTableItems"},
                                                       {State::ParseTableEnd, "ParseTableEnd"},
                                                       {State::ParseArrayStart, "ParseArrayStart"},
                                                       {State::ParseArrayValues, "ParseArrayValues"},
                                                       {State::ParseArrayEnd, "ParseArrayEnd"},
                                                       {State::ParseValue, "ParseValue"},
                                                       {State::PopScope, "PopScope"},
                                                       {State::Finish, "Finish"}};

    for (const auto& [state, value] : lut) {
        if (state == s) {
            return "\033[1;32m" + value + "\033[0m";
        }
    }

    assert(false && "bad state");
    return "";
}

std::string
repr(Scope s) {
    std::string result = "";
    std::vector<std::tuple<Scope, std::string>> lut = {
        {Scope::Section, "Section"},
        {Scope::Table, "Table"},
        {Scope::Array, "Array"},
    };

    for (const auto& [state, value] : lut) {
        if (state == s) {
            return "\033[1;33m" + value + "\033[0m";
        }
    }

    assert(false && "bad state");
    return "";
}

std::string
diffy::repr(Value& v) {
    if (v.is_table()) {
        return fmt::format("Table");
    } else if (v.is_array()) {
        return "Array";
    } else if (v.is_int()) {
        return fmt::format("Integer<{}>", v.as_int());
    } else if (v.is_float()) {
        return fmt::format("Float<{}>", v.as_int());
    } else if (v.is_bool()) {
        return fmt::format("Boolean<{}>", v.as_bool());
    } else if (v.is_string()) {
        return fmt::format("String<'{}'>", v.as_string());
    } else {
        assert(false && "bad state");
        return "NOT IMPLEMENTED";
    }
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