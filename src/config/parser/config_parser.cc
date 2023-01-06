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
using namespace tok2;

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
diffy::ParseResult::set_error(tok2::Token& token, std::string error_message) {
    this->kind = ParseErrorKind::Parsing;
    this->error = fmt::format("'{}' at line {} column {}", error_message, token.line, token.column);
}

bool
prepare_tokens(const std::string& input_data, std::vector<tok2::Token>& tokens, diffy::ParseResult& result) {
    diffy::tok2::ParseOptions tok2_options;
    tok2_options.strip_newlines = true;
    tok2_options.strip_spaces = true;
    tok2_options.strip_quotes = true;                   // drop "'" and '"'
    tok2_options.strip_annotated_string_tokens = true;  // drop '#' and '//' from comments
    tok2_options.strip_comments = false;
    tok2_options.append_terminator = true;  // Append termination token to avoid some bounds checking

    diffy::tok2::ParseResult tok2_result;
    if (!diffy::tok2::tokenize(input_data, tok2_options, tok2_result)) {
        result.kind = ParseErrorKind::Tokenization;
        result.error = tok2_result.error;
        return false;
    }

    // All good!
    tokens = tok2_result.tokens;
    result.kind = ParseErrorKind::None;
    return true;
}

bool
diffy::cfg_parse(const std::string& input_data,
                 diffy::ParseResult& result,
                 std::function<void(TbInstruction)> emit_cb) {
    std::vector<tok2::Token> input_tokens;
    if (!prepare_tokens(input_data, input_tokens, result)) {
        return false;
    }
    // tok2::token_dump(input_tokens, input_data);

    //
    // State machine "DSL"
    //
    // Oh no, macros. But it's the only way I can think of where we retain flow control without
    // resorting to hacks. The logic blocks makes it easier to reason about the parser behaviour.
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

    // TODO: Find a better way to do critical sections. See TODO further down.

    std::size_t cursor = 0;

    // clang-format off
    #define PARSER_NEXT_STATE() {                   \
        continue;                                   \
    }

    #define PARSER_NEXT_TOKEN() {                   \
        cursor++;                                   \
        token = input_tokens[cursor];               \
        if (token.id & tok2::TokenId_Terminator && !in_critical_section) {  \
            state = State::Finish;                  \
            PARSER_NEXT_STATE();                    \
        }                                           \
    }

    #define PARSER_EXPECT(expected_id) {                                                                   \
        if (!((expected_id) & token.id)) {                                                                 \
            result.set_error(token, fmt::format("Expected {}, found {} [src:{}]",                          \
                diffy::tok2::repr(expected_id), diffy::tok2::repr(token.id), __LINE__));                   \
            return false;                                                                                  \
        }                                                                                                  \
    }

    #define PARSER_EXPECT_AND_ADVANCE(expected_id) {                                                       \
        if (!((expected_id) & token.id)) {                                                                 \
            result.set_error(token, fmt::format("Expected {}, found {} [src:{}]",                          \
                diffy::tok2::repr(expected_id), diffy::tok2::repr(token.id), __LINE__));                   \
            return false;                                                                                  \
        } else {                                                                                           \
            PARSER_NEXT_TOKEN();                                                                           \
        }                                                                                                  \
    }

    #define PARSER_GIVE_UP(message) {                                                                  \
        if (token.id & tok2::TokenId_Terminator)                                                       \
            PARSER_NEXT_STATE();                                                                       \
        result.set_error(token, fmt::format("error: \033[1m'{}'\033[0m while processing {} [src: {}]", \
                message, diffy::tok2::repr(token.id), __LINE__));                                      \
        return false;                                                                                  \
    }

    #define PARSER_JUMP(next_state) {    \
        state = next_state;              \
        PARSER_NEXT_STATE();             \
    }

    #define PARSER_TRANSITION_TO(token_id, next_state) {    \
        if (token.id & (token_id)) {                        \
            state = next_state;                             \
            PARSER_NEXT_STATE();                            \
        }                                                   \
    }

    #define PARSER_ADVANCE_AND_TRANSITION_TO(token_id, next_state) {    \
        if (token.id & (token_id)) {                                    \
            state = next_state;                                         \
            PARSER_NEXT_TOKEN();                                        \
            PARSER_NEXT_STATE();                                        \
        }                                                               \
    }

    #define PARSER_CONSUME(expected_id, value_consumer) {          \
        PARSER_EXPECT(expected_id);                                \
        value_consumer(token.str_from(input_data));                \
        PARSER_NEXT_TOKEN();                                       \
    }

    #define PARSER_EAT_COMMENTS() {                                                     \
        while (token.id & tok2::TokenId_Comment) {                                      \
            emit_ins({TbOperator::Comment, token.str_from(input_data)},                 \
                         token.id & tok2::TokenId_FirstOnLine);                         \
            PARSER_NEXT_TOKEN();                                                        \
        }                                                                               \
    }

    #define PARSER_SKIP(ids) {    \
        if (token.id & (ids)) {   \
            PARSER_NEXT_TOKEN();  \
        }                         \
    }
    // clang-format on

    auto emit_ins = [&](TbInstruction ins, bool first_on_line = false) {
        static int cnt = 0;
        cnt++;
        TRACE("* Emit[{}] {} {}\n", cnt, repr(ins.op), ins.oparg1);
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
                if (state_selection_tokens[0].id & TokenId_Identifier) {
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
              repr(state), tok2::repr(token.id), token.str_from(input_data));

        using namespace tok2;
        switch (state) {
            case State::ParseSection: {
                PARSER_EAT_COMMENTS();
                // Expect [
                PARSER_EXPECT_AND_ADVANCE(TokenId_OpenBracket);

                scope_stack.push(Scope::Section);
                TRACE("* Pushing stack (section) {}\n", repr(scope_stack.top()));

                PARSER_CONSUME(TokenId_Identifier, ([&](const std::string& key) {
                                   emit_ins({TbOperator::Key, key});
                               }));

                emit_ins({TbOperator::TableStart, "from section"});

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
                PARSER_CONSUME(TokenId_Identifier, ([&](const std::string& key) {
                                   emit_ins({TbOperator::Key, key});
                               }));

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
                                   TbInstruction ins = {TbOperator::Value, value};
                                   if (token.id & TokenId_Boolean) {
                                       ins.oparg2_type = TbValueType::Bool;
                                       ins.oparg2_bool = token.token_boolean_arg;
                                   } else if (token.id & TokenId_Integer) {
                                       ins.oparg2_type = TbValueType::Int;
                                       ins.oparg2_int = token.token_int_arg;
                                   } else if (token.id & TokenId_String) {
                                       ins.oparg2_type = TbValueType::String;
                                   } else {
                                       assert(false && "Not reached");
                                       ins.oparg2_type = TbValueType::String;
                                   }
                                   emit_ins(ins);
                               }));

                PARSER_EAT_COMMENTS();

                switch (scope_stack.top()) {
                    case Scope::Section: {
                        PARSER_TRANSITION_TO(TokenId_Identifier, State::ParseKey);

                        emit_ins({TbOperator::TableEnd});
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

                emit_ins({TbOperator::TableStart});

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
                emit_ins({TbOperator::ArrayStart});

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
                PARSER_CONSUME(TokenId_Identifier, ([&](const std::string& key) {
                                   emit_ins({TbOperator::Key, key});
                               }));

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

                // TODO: something less hacky
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
                        emit_ins({TbOperator::TableEnd});
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
                            emit_ins({TbOperator::ArrayEnd});
                        } break;
                        case Scope::Section:
                            // fall-through
                        case Scope::Table: {
                            emit_ins({TbOperator::TableEnd});
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
                    comment_context_value->value_comments.push_back(ins.oparg1);
                    comment_context_value = nullptr;
                } else {
                    comments.push_back(ins.oparg1);
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
                last_key = ins.oparg1;
                comment_context_value = nullptr;
            } break;
            case TbOperator::Value: {
                Value value;

                if (ins.oparg2_type == TbValueType::Int) {
                    value = make_value(Value{Value::Int{ins.oparg2_int}});
                } else if (ins.oparg2_type == TbValueType::Bool) {
                    value = make_value(Value{Value::Bool{ins.oparg2_bool}});
                } else {
                    value = make_value(Value{Value::String{ins.oparg1}});
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

// ---

bool
cfg_value_contains(Value& v, const std::string& key) {
    if (v.is_table()) {
        return v.as_table().contains(key);
    }
    return false;
}

bool
cfg_value_is_array(Value& v) {
    return std::holds_alternative<Value::Array>(v.v);
}
bool
cfg_value_is_table(Value& v) {
    return std::holds_alternative<Value::Table>(v.v);
}
bool
cfg_value_is_int(Value& v) {
    return std::holds_alternative<Value::Int>(v.v);
}
bool
cfg_value_is_bool(Value& v) {
    return std::holds_alternative<Value::Bool>(v.v);
}
bool
cfg_value_is_string(Value& v) {
    return std::holds_alternative<Value::String>(v.v);
}

Value::Array&
cfg_value_as_array(Value& v) {
    return std::get<Value::Array>(v.v);
}
Value::Table&
cfg_value_as_table(Value& v) {
    return std::get<Value::Table>(v.v);
}
Value::Int&
cfg_value_as_int(Value& v) {
    return std::get<Value::Int>(v.v);
}
Value::Bool&
cfg_value_as_bool(Value& v) {
    return std::get<Value::Bool>(v.v);
}
Value::String&
cfg_value_as_string(Value& v) {
    return std::get<Value::String>(v.v);
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// -- -- --

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
    } else if (v.is_bool()) {
        return fmt::format("Boolean<{}>", v.as_bool());
    } else if (v.is_string()) {
        return fmt::format("String<'{}'>", v.as_string());
    } else {
        assert(false && "bad state");
        return "NOT IMPLEMENTED";
    }
}
